#pragma once
#include "ImageCompare.h"

#include <cmath>
#include <string>
#include <vector>

// MINOR_DIFF: images are not pixel-identical, but the structural diff found
// only anti-aliasing / edge-rendering jitter (no significant clustered change)
// — e.g. text/symbol edges or filled-contour boundaries rendered slightly
// differently between servers.  Kept distinct from EQUAL (they *do* differ) and
// from DIFFERENT (the difference is not significant).  Appended last so
// existing values keep their int codes.
enum class CompareStatus { PENDING, RUNNING, EQUAL, DIFFERENT, ERROR, TOO_LARGE, MINOR_DIFF };

// Detected semantic content category.  Ordered roughly from most- to
// least-specific so callers can compare with >=.
enum class ContentKind
{
  UNKNOWN,    // Not yet determined
  BINARY,     // Unrecognised binary blob
  IMAGE,      // Binary raster image (png, jpeg, gif, …)
  SVG,        // SVG (XML-based but renderable as image)
  PDF,        // PDF (rasterised for comparison)
  TEXT,       // Plain text
  JSON,       // JSON  – pretty-printed before diff
  XML,        // XML / HTML – pretty-printed before diff
  SERIAL,     // SmartMet "serial" output (PHP-serialize syntax) –
              // pretty-printed before diff
};

// Returns true when `kind` should be compared as a rendered image.
inline bool is_image_kind(ContentKind kind)
{
  return kind == ContentKind::IMAGE ||
         kind == ContentKind::SVG   ||
         kind == ContentKind::PDF;
}

struct QueryInfo
{
  std::string request_string;
  std::string time_utc;
};

struct FileLoadResult
{
  std::vector<QueryInfo> queries;
  std::string error;
  std::vector<std::pair<int, std::string>> problematic_lines; // {line_number, reason}
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

  // Image comparison metric: NaN = not computed, +inf = identical images.
  double psnr = std::numeric_limits<double>::quiet_NaN();

  // Structural (anti-aliasing-aware) image-diff summary.  Populated when both
  // sides are images that decode to non-identical pixels; its `differs` verdict
  // (not PSNR) drives the EQUAL/DIFFERENT status so that text/symbol rendering
  // jitter is not reported as a difference.  `computed` is false otherwise.
  ImageDiffResult image_diff;

  // Non-empty when "Ignore server host in response URLs" is active.
  // Each is the "host[:port]" portion of the respective server URL.
  // DiffView uses these to treat host-URL-only differences as equal.
  std::string host_port1;
  std::string host_port2;

  CompareStatus status = CompareStatus::PENDING;
};
