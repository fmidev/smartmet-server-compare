#include "CompareRunner.h"
#include "ContentHandler.h"
#include "HttpClient.h"
#include "ImageCompare.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

// ---------------------------------------------------------------------------
// CompareRunner
// ---------------------------------------------------------------------------

CompareRunner::CompareRunner()
{
  dispatcher_.connect(sigc::mem_fun(*this, &CompareRunner::on_dispatch));
}

CompareRunner::~CompareRunner()
{
  stop();
}

void CompareRunner::start(std::vector<QueryInfo> queries,
                          std::string server1_url,
                          std::string server2_url,
                          int max_concurrent,
                          size_t max_size,
                          bool ignore_host)
{
  stop();

  stop_requested_ = false;
  running_ = true;

  thread_ = std::thread(&CompareRunner::worker,
                        this,
                        std::move(queries),
                        std::move(server1_url),
                        std::move(server2_url),
                        std::max(1, max_concurrent),
                        max_size,
                        ignore_host);
}

void CompareRunner::request_stop()
{
  stop_requested_ = true;
  std::lock_guard<std::mutex> lock(active_queries_mutex_);
  for (auto* client : active_queries_)
    client->stop();
}

void CompareRunner::stop()
{
  request_stop();
  if (thread_.joinable())
    thread_.join();
  running_ = false;
}

void CompareRunner::on_dispatch()
{
  // Called on GTK main thread.
  std::deque<CompareResult> local;
  bool done = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    local.swap(result_queue_);
    done = done_pending_;
    done_pending_ = false;
  }

  for (auto& r : local)
    sig_result_.emit(std::move(r));

  if (done)
  {
    running_ = false;
    sig_done_.emit();
  }
}

// ---------------------------------------------------------------------------
// Host-normalization helpers
// ---------------------------------------------------------------------------

// Extract "host:port" (or just "host") from a URL like "http://host:port/path".
static std::string extract_host_port(const std::string& url)
{
  const auto sep = url.find("://");
  if (sep == std::string::npos) return {};
  const auto host_start = sep + 3;
  const auto path_start = url.find('/', host_start);
  return url.substr(host_start,
                    path_start == std::string::npos ? std::string::npos
                                                    : path_start - host_start);
}

// Replace every non-overlapping occurrence of needle in s with repl.
static std::string str_replace_all(std::string s,
                                    const std::string& needle,
                                    const std::string& repl)
{
  if (needle.empty()) return s;
  std::string out;
  out.reserve(s.size());
  std::size_t pos = 0;
  std::size_t found;
  while ((found = s.find(needle, pos)) != std::string::npos)
  {
    out.append(s, pos, found - pos);
    out += repl;
    pos = found + needle.size();
  }
  out.append(s, pos);
  return out;
}

// Replace all occurrences of the server's hostname (and hostname:port) in text
// with "SERVER_HOST" so that host-only URL differences don't show as diffs.
static std::string normalize_server_host(std::string text, const std::string& base_url)
{
  const std::string host_port = extract_host_port(base_url);
  if (host_port.empty()) return text;

  // Replace "host:port" first (more specific) before the bare-host pass.
  text = str_replace_all(std::move(text), host_port, "SERVER_HOST");

  // If base_url included a port, also replace the bare host without port.
  const auto colon = host_port.find(':');
  if (colon != std::string::npos)
  {
    const std::string host_only = host_port.substr(0, colon);
    if (!host_only.empty())
      text = str_replace_all(std::move(text), host_only, "SERVER_HOST");
  }

  return text;
}

// ---------------------------------------------------------------------------

// Ensure server_url ends without a slash so that
// server_url + request_string (which starts with /) is well-formed.
static std::string normalize_base(const std::string& url)
{
  if (!url.empty() && url.back() == '/')
    return url.substr(0, url.size() - 1);
  return url;
}

void CompareRunner::worker(std::vector<QueryInfo> queries,
                           std::string server1_url,
                           std::string server2_url,
                           int max_concurrent,
                           size_t max_size,
                           bool ignore_host)
{
  const std::string base1 = normalize_base(server1_url);
  const std::string base2 = normalize_base(server2_url);

  const int total = static_cast<int>(queries.size());
  std::atomic<int> next_idx{0};

  auto task = [&]()
  {
    while (!stop_requested_)
    {
      const int i = next_idx.fetch_add(1, std::memory_order_relaxed);
      if (i >= total)
        break;

      const auto& q = queries[i];
      CompareResult result;
      result.index = i;
      result.request_string = q.request_string;
      result.status = CompareStatus::RUNNING;

      {
        std::lock_guard<std::mutex> lock(mutex_);
        result_queue_.push_back(result);
      }
      dispatcher_.emit();

      HttpClient client(60);
      client.add("s1", base1 + q.request_string);
      client.add("s2", base2 + q.request_string);

      {
        std::lock_guard<std::mutex> lock(active_queries_mutex_);
        active_queries_.insert(&client);
      }
      client.execute();
      {
        std::lock_guard<std::mutex> lock(active_queries_mutex_);
        active_queries_.erase(&client);
      }

      const auto& r1 = client.response("s1");
      const auto& r2 = client.response("s2");

      result.body1 = r1.body;
      result.body2 = r2.body;
      result.content_type1 = r1.content_type;
      result.content_type2 = r2.content_type;
      result.status_code1 = r1.status_code;
      result.status_code2 = r2.status_code;
      result.error1 = r1.error;
      result.error2 = r2.error;

      if (!result.error1.empty() || !result.error2.empty())
      {
        result.status = CompareStatus::ERROR;
      }
      else if (max_size > 0 && (result.body1.size() > max_size || result.body2.size() > max_size))
      {
        if (result.body1.size() > max_size)
          result.error1 = "Response size exceeds limit";
        if (result.body2.size() > max_size)
          result.error2 = "Response size exceeds limit";
        result.status = CompareStatus::TOO_LARGE;
      }
      else if (result.status_code1 >= 400 && result.status_code1 == result.status_code2)
      {
        // Both servers agree on the same error status — treat as equal without
        // diffing bodies (error-message text differences are usually noise).
        result.status = CompareStatus::EQUAL;
      }
      else
      {
        result.kind1 = detect_content_kind(result.content_type1, result.body1);
        result.kind2 = detect_content_kind(result.content_type2, result.body2);

        const bool img1 = is_image_kind(result.kind1);
        const bool img2 = is_image_kind(result.kind2);

        if (img1 && img2)
        {
          if (result.body1 == result.body2)
          {
            // Byte-identical — skip expensive Magick++ decoding.
            result.psnr = std::numeric_limits<double>::infinity();
            result.status = CompareStatus::EQUAL;
          }
          else
          {
            try
            {
              result.psnr = compute_psnr(result.body1, result.body2);
              // Different bytes can still decode to identical pixels (e.g.
              // re-encoded PNGs).  Treat MSE=0 (PSNR=∞) as equal — the value
              // still shows in the list so the match is visible.
              result.status = std::isinf(result.psnr) ? CompareStatus::EQUAL
                                                      : CompareStatus::DIFFERENT;
            }
            catch (const std::exception& e)
            {
              result.error1 = e.what();
              result.status = CompareStatus::ERROR;
            }
          }
        }
        else if (img1 != img2)
        {
          const auto& text_kind = img1 ? result.kind2 : result.kind1;
          const auto& text_body = img1 ? result.body2 : result.body1;

          auto [fmt, ferr] = format_for_diff(text_kind, text_body);
          auto& fmt_slot = img1 ? result.formatted2 : result.formatted1;
          auto& err_slot = img1 ? result.error2 : result.error1;
          fmt_slot = fmt.empty() ? text_body : std::move(fmt);
          if (!ferr.empty()) err_slot = ferr;

          result.status = CompareStatus::ERROR;
        }
        else
        {
          auto [fmt1, ferr1] = format_for_diff(result.kind1, result.body1);
          auto [fmt2, ferr2] = format_for_diff(result.kind2, result.body2);

          result.formatted1 = std::move(fmt1);
          result.formatted2 = std::move(fmt2);

          if (!ferr1.empty()) result.error1 = ferr1;
          if (!ferr2.empty()) result.error2 = ferr2;

          if (ignore_host)
          {
            result.host_port1 = extract_host_port(base1);
            result.host_port2 = extract_host_port(base2);
          }

          const bool use_text = !result.formatted1.empty() || !result.formatted2.empty();
          bool equal;
          if (use_text && ignore_host)
          {
            // Equality check on normalized copies so host-URL-only differences
            // are treated as equal without altering the displayed text.
            equal = normalize_server_host(result.formatted1, base1) ==
                    normalize_server_host(result.formatted2, base2);
          }
          else
          {
            equal = use_text ? (result.formatted1 == result.formatted2)
                             : (result.body1 == result.body2);
          }
          result.status = equal ? CompareStatus::EQUAL : CompareStatus::DIFFERENT;
        }
      }

      {
        std::lock_guard<std::mutex> lock(mutex_);
        result_queue_.push_back(std::move(result));
      }
      dispatcher_.emit();
    }
  };

  const int n_threads = std::min(max_concurrent, total);
  std::vector<std::thread> threads;
  threads.reserve(n_threads);
  for (int t = 0; t < n_threads; ++t)
    threads.emplace_back(task);
  for (auto& t : threads)
    t.join();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    done_pending_ = true;
  }
  dispatcher_.emit();
}
