#pragma once
#include <string>

// Percent-decode a URL-encoded string (%20 → space, + → space, etc.).
std::string urldecode(const std::string& input);

// Percent-encode a string for use as a URL query parameter key or value.
// Encodes everything except RFC 3986 unreserved characters (A-Z a-z 0-9 - _ . ~).
std::string urlencode(const std::string& input);

// Turn a request pasted by the user into the request-string form used
// throughout the tool (starts with '/', no host part).  Surrounding
// whitespace is trimmed and a leading "scheme://host[:port]" or bare
// "host[:port]" is removed, so "http://host:8080/wms?x=1", "host/wms?x=1"
// and "/wms?x=1" all yield "/wms?x=1"; a URL with no path gives "/".
// Returns an empty string only for blank input.
std::string strip_url_host(const std::string& input);
