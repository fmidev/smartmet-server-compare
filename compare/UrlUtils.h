#pragma once
#include <string>

// Percent-decode a URL-encoded string (%20 → space, + → space, etc.).
std::string urldecode(const std::string& input);

// Percent-encode a string for use as a URL query parameter key or value.
// Encodes everything except RFC 3986 unreserved characters (A-Z a-z 0-9 - _ . ~).
std::string urlencode(const std::string& input);
