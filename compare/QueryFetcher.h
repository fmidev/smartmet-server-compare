#pragma once
#include "Types.h"

#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

/**
 * Fetches the "lastrequests" list from a SmartMet server's admin endpoint,
 * parses the JSON response, and filters entries whose RequestString starts with
 * a given prefix.
 */
class QueryFetcher
{
 public:
  using Callback = std::function<void(std::vector<QueryInfo> queries, std::string error)>;

  // Synchronous fetch – blocks until complete or until an error occurs.
  // Returns {queries, ""} on success, {{}, error_message} on failure.
  static std::pair<std::vector<QueryInfo>, std::string> fetch(const std::string& server_url,
                                                              const std::string& prefix,
                                                              int minutes);

  // Asynchronous fetch – spawns a background thread, calls cb on *that* thread
  // when done.  The caller is responsible for dispatching the result back to
  // the main thread (e.g. via Glib::Dispatcher).
  void fetch_async(std::string server_url, std::string prefix, int minutes, Callback cb);

  ~QueryFetcher();

 private:
  std::thread thread_;
};
