#pragma once
#include <atomic>
#include <map>
#include <string>
#include <vector>

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
 *
 * Persistent connections (HTTP keep-alive) are optional and off by default.
 * When enabled, connections survive the HttpClient object and are reused by
 * the next HttpClient created on the same thread, so a run of N queries costs
 * one TCP (and TLS) handshake per server instead of N.  See execute() for why
 * the cache is per thread.  Response::connection_reused reports, per request,
 * whether an existing connection was actually picked up.
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

    // Raw HTTP headers as sent/received on the wire.  Both are filled on
    // every request (including failures, when curl got as far as talking
    // HTTP) and include the request/status line.  Useful for displaying a
    // "curl -v" style transcript of the exchange.
    std::string request_headers;
    std::string response_headers;

    // True when this request travelled over a connection that was already
    // open, i.e. the server honoured keep-alive and the client reused it.
    // Always false when keep-alive is disabled.
    bool connection_reused = false;
  };

  explicit HttpClient(int timeout_sec = 60, bool keep_alive = false);

  // Queue a GET request.
  void add(const std::string& id, const std::string& url);

  // Execute all queued requests in parallel.  Blocks until all finish or
  // stop() is called.
  void execute();

  // Interrupt in-flight requests.  Thread-safe.
  void stop();

  // Retrieve the response for a given id (valid after execute() returns).
  const Response& response(const std::string& id) const;

  // Drop every connection cached for the calling thread.  Only needed when a
  // server is restarted underneath a long-lived worker thread; the cache is
  // otherwise released automatically when the thread exits.
  static void close_idle_connections();

 private:
  // Run the given request ids and return the ids that failed in a way that a
  // fresh connection could fix.  With fresh_connect the transfers are forced
  // onto brand new connections and nothing is proposed for another retry.
  std::vector<std::string> perform(const std::vector<std::string>& ids, bool fresh_connect);

  int timeout_sec_;
  bool keep_alive_;
  std::atomic<bool> stopped_{false};

  struct Request
  {
    std::string url;
    Response resp;
  };
  std::map<std::string, Request> requests_;
};
