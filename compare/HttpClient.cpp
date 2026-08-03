#include "HttpClient.h"

#include <curl/curl.h>

#include <stdexcept>

// ---------------------------------------------------------------------------
// One-time libcurl global init
// ---------------------------------------------------------------------------

static void ensure_curl_init()
{
  static bool once = [] {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
  }();
  (void)once;
}

// ---------------------------------------------------------------------------
// libcurl callbacks
// ---------------------------------------------------------------------------

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  auto* buf = static_cast<std::string*>(userdata);
  const size_t bytes = size * nmemb;
  buf->append(ptr, bytes);
  return bytes;
}

// Response-header callback: libcurl delivers each header line separately,
// including the status line and the terminating blank line.  Append the
// whole block verbatim.
static size_t header_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  auto* buf = static_cast<std::string*>(userdata);
  const size_t bytes = size * nmemb;
  buf->append(ptr, bytes);
  return bytes;
}

// Debug callback used only to capture the outgoing request headers
// (CURLINFO_HEADER_OUT).  Other debug types are ignored.
static int debug_cb(CURL*, curl_infotype type, char* data, size_t size, void* userptr)
{
  if (type == CURLINFO_HEADER_OUT)
  {
    auto* buf = static_cast<std::string*>(userptr);
    buf->append(data, size);
  }
  return 0;
}

// Extract bare content-type (before the first ';')
static std::string base_content_type(const char* ct)
{
  if (!ct)
    return {};
  std::string s(ct);
  auto pos = s.find(';');
  if (pos != std::string::npos)
    s.resize(pos);
  // Trim trailing whitespace
  while (!s.empty() && s.back() == ' ')
    s.pop_back();
  return s;
}

// ---------------------------------------------------------------------------
// Per-thread connection cache
// ---------------------------------------------------------------------------

// libcurl keeps its pool of live connections in the *multi* handle, not in the
// easy handles: removing an easy handle returns its connection to the multi's
// cache, where the next transfer to the same origin picks it up.  Creating a
// fresh multi handle for every execute() therefore throws every connection
// away no matter what the server says about keep-alive, which is what this
// client did before the option existed.
//
// A CURLM handle is not thread safe, so the cache is thread_local rather than
// global: each CompareRunner worker thread gets its own, no locking is needed
// on the hot path, and curl_multi_cleanup() runs when the thread exits.  With
// two target servers per thread the cache stays tiny.
namespace
{
struct MultiHandleCache
{
  CURLM* handle = nullptr;

  CURLM* get()
  {
    if (!handle)
    {
      handle = curl_multi_init();
      if (handle)
      {
        // A worker thread only ever talks to the two servers under comparison;
        // anything beyond a handful of cached connections is dead weight.
        curl_multi_setopt(handle, CURLMOPT_MAXCONNECTS, 8L);
      }
    }
    return handle;
  }

  void reset()
  {
    if (handle)
    {
      curl_multi_cleanup(handle);
      handle = nullptr;
    }
  }

  ~MultiHandleCache() { reset(); }
};

MultiHandleCache& thread_cache()
{
  static thread_local MultiHandleCache cache;
  return cache;
}
}  // namespace

void HttpClient::close_idle_connections()
{
  thread_cache().reset();
}

// ---------------------------------------------------------------------------
// HttpClient
// ---------------------------------------------------------------------------

HttpClient::HttpClient(int timeout_sec, bool keep_alive)
    : timeout_sec_(timeout_sec), keep_alive_(keep_alive)
{
  ensure_curl_init();
}

void HttpClient::add(const std::string& id, const std::string& url)
{
  requests_[id].url = url;
}

void HttpClient::stop()
{
  stopped_ = true;
}

static int progress_cb(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
  auto* stopped = static_cast<std::atomic<bool>*>(clientp);
  return stopped->load() ? 1 : 0;  // non-zero aborts the transfer
}

// Errors a server can produce by closing a persistent connection underneath the
// client.  These say "the connection went away", as opposed to "the server
// refused me" or "the name does not resolve", which a retry would not fix.
static bool is_dropped_connection(CURLcode code)
{
  switch (code)
  {
    case CURLE_GOT_NOTHING:   // closed before any response byte arrived
    case CURLE_RECV_ERROR:    // reset while reading the response
    case CURLE_SEND_ERROR:    // reset while writing the request
    case CURLE_PARTIAL_FILE:  // closed after part of the body
      return true;
    default:
      return false;
  }
}

void HttpClient::execute()
{
  stopped_ = false;

  std::vector<std::string> ids;
  ids.reserve(requests_.size());
  for (const auto& [id, req] : requests_)
    ids.push_back(id);

  const std::vector<std::string> retry = perform(ids, /*fresh_connect=*/false);

  // A server may close a persistent connection at any time, and the client has
  // no way to tell a connection that is about to be closed from a live one --
  // it finds out only when the request it has just written goes unanswered.
  // libcurl retries on its own while nothing has been received yet, but not
  // once part of a response has arrived, so a server dropping a reused
  // connection surfaces as a hard error ("Transferred a partial file", "Recv
  // failure: Connection reset by peer", ...).
  //
  // Retry those on a forced-fresh connection.  Only requests that actually
  // travelled over a cached connection qualify, so a server that is genuinely
  // down is not contacted twice, and every request here is a GET, so repeating
  // one cannot repeat a side effect.
  if (!retry.empty() && !stopped_)
  {
    for (const auto& id : retry)
      requests_[id].resp = Response{};
    perform(retry, /*fresh_connect=*/true);
  }
}

std::vector<std::string> HttpClient::perform(const std::vector<std::string>& ids,
                                             bool fresh_connect)
{
  std::vector<std::string> retryable;

  // With keep-alive the multi handle (and thus its connection cache) outlives
  // this HttpClient; without it, a private handle is created and destroyed per
  // execute() so that no connection can survive.
  CURLM* multi = keep_alive_ ? thread_cache().get() : curl_multi_init();
  if (!multi)
    throw std::runtime_error("curl_multi_init failed");

  // Create one easy handle per request
  std::map<CURL*, std::string> handle_to_id;

  for (const auto& id : ids)
  {
    auto& req = requests_[id];

    CURL* easy = curl_easy_init();
    if (!easy)
      continue;

    curl_easy_setopt(easy, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, static_cast<long>(timeout_sec_));
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &req.resp.body);

    // Capture headers in both directions for "curl -v" style transcripts.
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, &req.resp.response_headers);
    curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(easy, CURLOPT_DEBUGFUNCTION, debug_cb);
    curl_easy_setopt(easy, CURLOPT_DEBUGDATA, &req.resp.request_headers);

    // Progress callback for cancellation support
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, &stopped_);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);

    // Accept any encoding the server might use
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");

    if (!keep_alive_)
    {
      // Belt and braces: the private multi handle is destroyed below anyway,
      // but this also makes libcurl announce "Connection: close" so the server
      // is told not to hold the socket open on its side either.
      curl_easy_setopt(easy, CURLOPT_FORBID_REUSE, 1L);
    }
    else if (fresh_connect)
    {
      // Retry of a request whose cached connection the server dropped: skip the
      // cache entirely.  The new connection is still kept for later requests.
      curl_easy_setopt(easy, CURLOPT_FRESH_CONNECT, 1L);
    }

    curl_multi_add_handle(multi, easy);
    handle_to_id[easy] = id;
  }

  // Run the multi loop
  int still_running = 0;
  curl_multi_perform(multi, &still_running);

  while (still_running > 0 && !stopped_)
  {
    int numfds = 0;
    curl_multi_poll(multi, nullptr, 0, 200, &numfds);
    curl_multi_perform(multi, &still_running);
  }

  // Collect results
  CURLMsg* msg;
  int msgs_left;
  while ((msg = curl_multi_info_read(multi, &msgs_left)))
  {
    if (msg->msg != CURLMSG_DONE)
      continue;

    CURL* easy = msg->easy_handle;
    auto it = handle_to_id.find(easy);
    if (it == handle_to_id.end())
      continue;

    auto& resp = requests_[it->second].resp;

    // CURLINFO_NUM_CONNECTS counts the connections libcurl had to *create* for
    // this transfer, so zero means it went over one that was already cached.
    long connects = 0;
    const bool have_connects =
        curl_easy_getinfo(easy, CURLINFO_NUM_CONNECTS, &connects) == CURLE_OK;
    const bool used_cached_connection = have_connects && connects == 0;

    if (msg->data.result != CURLE_OK)
    {
      resp.error = curl_easy_strerror(msg->data.result);

      // Only a failure on a connection we took from the cache is a candidate
      // for the fresh-connection retry.  A failure on a connection libcurl had
      // just opened is a real problem with the server, not a dropped keep-alive
      // (and libcurl has already done its own retry by then, which is why this
      // does not loop).
      if (keep_alive_ && !fresh_connect && used_cached_connection &&
          is_dropped_connection(msg->data.result))
      {
        retryable.push_back(it->second);
      }
    }
    else
    {
      resp.connection_reused = keep_alive_ && used_cached_connection;

      long code = 0;
      curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
      resp.status_code = static_cast<int>(code);

      char* ct = nullptr;
      curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &ct);
      resp.content_type = base_content_type(ct);
    }
  }

  // Cleanup.  Removing an easy handle hands its connection back to the multi
  // handle's cache, so with keep-alive the connections stay open for the next
  // execute() on this thread; without it the multi handle goes away with them.
  for (auto& [easy, id] : handle_to_id)
  {
    curl_multi_remove_handle(multi, easy);
    curl_easy_cleanup(easy);
  }
  if (!keep_alive_)
    curl_multi_cleanup(multi);

  return retryable;
}

const HttpClient::Response& HttpClient::response(const std::string& id) const
{
  static const Response empty;
  auto it = requests_.find(id);
  return it != requests_.end() ? it->second.resp : empty;
}
