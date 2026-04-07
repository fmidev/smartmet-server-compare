#pragma once
#include "Types.h"

#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

/**
 * Fetches the "lastrequests" list from a SmartMet server's admin endpoint,
 * parses the JSON response, and filters entries whose RequestString starts with
 * a given prefix.
 *
 * Alternatively, queries can be loaded from a plain-text file (one request
 * string per line).  In that case only deduplication is applied; no prefix
 * filtering is performed.
 */
class QueryFetcher
{
 public:
  using Callback = std::function<void(std::vector<QueryInfo> queries, std::string error)>;

  // Synchronous fetch from server admin endpoint.
  // Returns {queries, ""} on success, {{}, error_message} on failure.
  static std::pair<std::vector<QueryInfo>, std::string> fetch(const std::string& server_url,
                                                              const std::string& prefix,
                                                              int minutes);

  // Load request strings from a plain-text file (one per line).
  // Lines that are empty or start with '#' are ignored.  Duplicates are
  // removed; no prefix filtering is applied.
  static std::pair<std::vector<QueryInfo>, std::string> fetch_from_file(
      const std::filesystem::path& path);

  // Asynchronous fetch from server – spawns a background thread, calls cb on
  // *that* thread when done.  The caller dispatches the result to the main thread.
  void fetch_async(std::string server_url, std::string prefix, int minutes, Callback cb);

  ~QueryFetcher();

 private:
  std::thread thread_;
};
