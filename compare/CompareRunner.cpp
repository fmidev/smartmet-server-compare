#include "CompareRunner.h"
#include "ContentHandler.h"
#include "HttpClient.h"
#include "ImageCompare.h"

#include <algorithm>
#include <cmath>
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
                          size_t max_size)
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
                        max_size);
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
                           size_t max_size)
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
      else
      {
        result.kind1 = detect_content_kind(result.content_type1, result.body1);
        result.kind2 = detect_content_kind(result.content_type2, result.body2);

        const bool img1 = is_image_kind(result.kind1);
        const bool img2 = is_image_kind(result.kind2);

        if (img1 && img2)
        {
          try
          {
            result.psnr = compute_psnr(result.body1, result.body2);
            result.status = std::isinf(result.psnr) ? CompareStatus::EQUAL
                                                    : CompareStatus::DIFFERENT;
          }
          catch (const std::exception& e)
          {
            result.error1 = e.what();
            result.status = CompareStatus::ERROR;
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

          const bool use_text = !result.formatted1.empty() || !result.formatted2.empty();
          const bool equal = use_text ? (result.formatted1 == result.formatted2)
                                      : (result.body1 == result.body2);
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
