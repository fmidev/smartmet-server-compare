#pragma once
#include <string>

// Percent-decode a URL-encoded string (%20 → space, etc.).
std::string urldecode(const std::string& input);
