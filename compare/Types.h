#pragma once
#include <string>

enum class CompareStatus { PENDING, RUNNING, EQUAL, DIFFERENT, ERROR };

// Detected semantic content category.  Ordered roughly from most- to
// least-specific so callers can compare with >=.
enum class ContentKind
{
  UNKNOWN,    // Not yet determined
  BINARY,     // Unrecognised binary blob
  IMAGE,      // Binary image (png, jpeg, gif, …) – reserved for future display
  TEXT,       // Plain text
  JSON,       // JSON  – pretty-printed before diff
  XML,        // XML / HTML / SVG – pretty-printed before diff
};

struct QueryInfo
{
  std::string request_string;
  std::string time_utc;
};

struct CompareResult
{
  int index = -1;
  std::string request_string;

  // Raw response bodies (decoded from HTTP)
  std::string body1;
  std::string body2;

  // Bodies formatted for diffing (pretty-printed for JSON/XML; same as body
  // for plain text; empty for binary/image → fall back to byte comparison)
  std::string formatted1;
  std::string formatted2;

  // Detected content kind for each side
  ContentKind kind1 = ContentKind::UNKNOWN;
  ContentKind kind2 = ContentKind::UNKNOWN;

  // Content-Type headers from the responses
  std::string content_type1;
  std::string content_type2;

  // HTTP status codes
  int status_code1 = 0;
  int status_code2 = 0;

  // Network / HTTP error descriptions (empty = no error)
  std::string error1;
  std::string error2;

  CompareStatus status = CompareStatus::PENDING;
};
