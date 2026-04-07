#include "UrlUtils.h"

#include <regex>

std::optional<ServerAddress> parse_server_url(const std::string& url)
{
  // Accepts  http://host:port[/path]  or  http://host[/path]
  static const std::regex re(R"(https?://([^/:]+)(?::(\d+))?(?:/.*)?)",
                              std::regex::ECMAScript);
  std::smatch m;
  if (!std::regex_match(url, m, re))
    return std::nullopt;

  ServerAddress addr;
  addr.host = m[1].str();
  addr.port = m[2].matched ? m[2].str() : "80";
  return addr;
}

std::string build_http_request(const std::string& host,
                               const std::string& path_and_query)
{
  return "GET " + path_and_query +
         " HTTP/1.1\r\n"
         "Host: " +
         host +
         "\r\n"
         "Accept: */*\r\n"
         "Connection: close\r\n"
         "\r\n";
}
