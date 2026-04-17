#pragma once
#include <atomic>
#include <map>
#include <string>

/**
 * Simple parallel HTTP/HTTPS client using libcurl.
 *
 * Usage:
 *   HttpClient client(60);
 *   client.add("s1", "http://server1:8080/path?q=1");
 *   client.add("s2", "https://server2/path?q=1");
 *   client.execute();
 *   auto r = client.response("s1");
 *
 * All added requests are executed in parallel via curl_multi.
 * Thread-safe stop() can be called from another thread to abort.
 */
class HttpClient
{
 public:
  struct Response
  {
    std::string body;
    std::string content_type;   // bare type (before ';')
    int status_code = 0;
    std::string error;          // empty on success
  };

  explicit HttpClient(int timeout_sec = 60);

  // Queue a GET request.
  void add(const std::string& id, const std::string& url);

  // Execute all queued requests in parallel.  Blocks until all finish or
  // stop() is called.
  void execute();

  // Interrupt in-flight requests.  Thread-safe.
  void stop();

  // Retrieve the response for a given id (valid after execute() returns).
  const Response& response(const std::string& id) const;

 private:
  int timeout_sec_;
  std::atomic<bool> stopped_{false};

  struct Request
  {
    std::string url;
    Response resp;
  };
  std::map<std::string, Request> requests_;
};
