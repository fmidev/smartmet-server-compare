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
 * Alternatively, queries can be loaded from a text file.  Two formats are
 * auto-detected per line:
 *   - plain request line:  starts with '/' (host-less path + query)
 *   - SmartMet access-log line:
 *       IP - - [ts] "METHOD path HTTP/ver" status [ts2] ...
 *     all GET/POST/HEAD entries are imported regardless of status code.
 * In both cases duplicates are removed; no prefix filtering is performed.
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

  // Load request strings from a text file.  Each line may be either a plain
  // request string (starting with '/') or a SmartMet access-log line; the
  // format is auto-detected per line.  Access-log entries are imported
  // regardless of HTTP status.  Empty lines, lines starting with '#', and
  // unrecognised lines are skipped.  Duplicates are removed; no prefix
  // filtering is applied.
  static std::pair<std::vector<QueryInfo>, std::string> fetch_from_file(
      const std::filesystem::path& path);

  // Asynchronous fetch from server – spawns a background thread, calls cb on
  // *that* thread when done.  The caller dispatches the result to the main thread.
  void fetch_async(std::string server_url, std::string prefix, int minutes, Callback cb);

  ~QueryFetcher();

 private:
  std::thread thread_;
};
