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
// HttpClient
// ---------------------------------------------------------------------------

HttpClient::HttpClient(int timeout_sec) : timeout_sec_(timeout_sec)
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

void HttpClient::execute()
{
  stopped_ = false;

  CURLM* multi = curl_multi_init();
  if (!multi)
    throw std::runtime_error("curl_multi_init failed");

  // Create one easy handle per request
  std::map<CURL*, std::string> handle_to_id;

  for (auto& [id, req] : requests_)
  {
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

    if (msg->data.result != CURLE_OK)
    {
      resp.error = curl_easy_strerror(msg->data.result);
    }
    else
    {
      long code = 0;
      curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
      resp.status_code = static_cast<int>(code);

      char* ct = nullptr;
      curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &ct);
      resp.content_type = base_content_type(ct);
    }
  }

  // Cleanup
  for (auto& [easy, id] : handle_to_id)
  {
    curl_multi_remove_handle(multi, easy);
    curl_easy_cleanup(easy);
  }
  curl_multi_cleanup(multi);
}

const HttpClient::Response& HttpClient::response(const std::string& id) const
{
  static const Response empty;
  auto it = requests_.find(id);
  return it != requests_.end() ? it->second.resp : empty;
}
