#pragma once
#include <optional>
#include <string>

struct ServerAddress
{
  std::string host;
  std::string port;  // string form required by TcpMultiQuery::add_query
};

// Parse "http://hostname:port[/...]" → ServerAddress.
// Returns nullopt when the URL cannot be parsed.
std::optional<ServerAddress> parse_server_url(const std::string& url);

// Build a minimal HTTP/1.1 GET request suitable for sending over a raw TCP
// connection (as required by TcpMultiQuery::add_query).
std::string build_http_request(const std::string& host,
                               const std::string& path_and_query);
