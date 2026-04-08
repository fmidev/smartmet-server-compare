#include "CompareRunner.h"
#include "ContentHandler.h"
#include "UrlUtils.h"

#include <smartmet/spine/HTTP.h>
#include <smartmet/spine/TcpMultiQuery.h>

#include <algorithm>
#include <cctype>
#include <string>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Extract bare content-type (before the first ';')
static std::string base_content_type(const std::string& ct)
{
  auto pos = ct.find(';');
  if (pos == std::string::npos)
    return ct;
  return ct.substr(0, pos);
}

struct ResponseInfo
{
  std::string body;
  std::string content_type;
  int status_code = 0;
  std::string error;
};

static ResponseInfo parse_raw_response(const SmartMet::Spine::TcpMultiQuery::Response& resp)
{
  ResponseInfo info;

  if (resp.error_code)
  {
    info.error = resp.error_desc;
    return info;
  }

  auto [status, response_ptr] = SmartMet::Spine::HTTP::parseResponseFull(resp.body);

  if (status != SmartMet::Spine::HTTP::ParsingStatus::COMPLETE || !response_ptr)
  {
    info.error = "Failed to parse HTTP response";
    // Try to return raw body so user can see something
    info.body = resp.body;
    return info;
  }

  info.status_code = static_cast<int>(response_ptr->getStatus());
  info.body = response_ptr->getDecodedContent();

  auto ct_header = response_ptr->getHeader("Content-Type");
  if (ct_header)
    info.content_type = base_content_type(*ct_header);

  return info;
}

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

void CompareRunner::stop()
{
  stop_requested_ = true;
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

void CompareRunner::worker(std::vector<QueryInfo> queries,
                           std::string server1_url,
                           std::string server2_url,
                           int max_concurrent,
                           size_t max_size)
{
  auto addr1 = parse_server_url(server1_url);
  auto addr2 = parse_server_url(server2_url);

  const int total = static_cast<int>(queries.size());
  std::atomic<int> next_idx{0};

  // Each sub-thread claims the next unclaimed query via next_idx and processes
  // it independently.  All sub-threads share the dispatcher and result_queue_.
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

      // Notify UI that this query is now in flight
      {
        std::lock_guard<std::mutex> lock(mutex_);
        result_queue_.push_back(result);
      }
      dispatcher_.emit();

      if (!addr1 || !addr2)
      {
        result.error1 = addr1 ? "" : "Invalid server 1 URL";
        result.error2 = addr2 ? "" : "Invalid server 2 URL";
        result.status = CompareStatus::ERROR;
      }
      else
      {
        const std::string req1 = build_http_request(addr1->host, q.request_string);
        const std::string req2 = build_http_request(addr2->host, q.request_string);

        SmartMet::Spine::TcpMultiQuery mq(60);
        mq.add_query("s1", addr1->host, addr1->port, req1);
        mq.add_query("s2", addr2->host, addr2->port, req2);
        mq.execute();

        auto info1 = parse_raw_response(mq["s1"]);
        auto info2 = parse_raw_response(mq["s2"]);

        result.body1 = std::move(info1.body);
        result.body2 = std::move(info2.body);
        result.content_type1 = std::move(info1.content_type);
        result.content_type2 = std::move(info2.content_type);
        result.status_code1 = info1.status_code;
        result.status_code2 = info2.status_code;
        result.error1 = std::move(info1.error);
        result.error2 = std::move(info2.error);

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

          auto [fmt1, ferr1] = format_for_diff(result.kind1, result.body1);
          auto [fmt2, ferr2] = format_for_diff(result.kind2, result.body2);
          result.formatted1 = std::move(fmt1);
          result.formatted2 = std::move(fmt2);

          // Append any formatting errors to the network-error fields so they
          // surface in the UI without hiding the actual content.
          if (!ferr1.empty()) result.error1 = ferr1;
          if (!ferr2.empty()) result.error2 = ferr2;

          // Compare the normalised form; fall back to raw bytes for binary.
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

  // Spawn up to max_concurrent sub-threads (but never more than queries to run)
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
