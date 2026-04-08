#include "ContentHandler.h"

#include <json/json.h>
#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string_view trim_front(std::string_view s)
{
  while (!s.empty() && static_cast<unsigned char>(s.front()) <= ' ')
    s.remove_prefix(1);
  return s;
}

// True when ≥ 90 % of the first N bytes are printable ASCII / UTF-8 lead bytes.
static bool looks_like_text(const std::string& body, std::size_t sample = 512)
{
  if (body.empty())
    return true;
  const std::size_t n = std::min(body.size(), sample);
  int printable = 0;
  for (std::size_t i = 0; i < n; ++i)
  {
    unsigned char c = static_cast<unsigned char>(body[i]);
    if (c >= 0x20 || c == '\n' || c == '\r' || c == '\t')
      ++printable;
  }
  return printable * 100 / static_cast<int>(n) >= 90;
}

// Classify by the Content-Type string alone (returns UNKNOWN when the header
// gives no clear answer).
static ContentKind kind_from_header(const std::string& ct)
{
  // Helper: case-insensitive substring test
  auto has = [&](const char* needle) {
    return ct.find(needle) != std::string::npos;
  };

  if (has("json"))
    return ContentKind::JSON;

  // SVG is XML
  if (has("svg"))
    return ContentKind::XML;

  if (has("xml") || has("xhtml") || has("gml") || has("kml") || has("wfs") || has("wms"))
    return ContentKind::XML;

  if (ct.find("text/html") != std::string::npos)
    return ContentKind::XML;  // treat HTML as XML for pretty-printing

  if (ct.find("text/") == 0)
    return ContentKind::TEXT;

  if (has("image/"))
    return ContentKind::IMAGE;

  return ContentKind::UNKNOWN;
}

// Body sniffing – used when the header is absent / generic.
static ContentKind sniff_body(const std::string& body)
{
  if (body.empty())
    return ContentKind::BINARY;

  // PNG magic
  if (body.size() >= 4 &&
      static_cast<unsigned char>(body[0]) == 0x89 &&
      body[1] == 'P' && body[2] == 'N' && body[3] == 'G')
    return ContentKind::IMAGE;

  // JPEG magic
  if (body.size() >= 2 &&
      static_cast<unsigned char>(body[0]) == 0xFF &&
      static_cast<unsigned char>(body[1]) == 0xD8)
    return ContentKind::IMAGE;

  // GIF magic
  if (body.size() >= 4 && body.substr(0, 4) == "GIF8")
    return ContentKind::IMAGE;

  const std::string_view front = trim_front(std::string_view(body));
  if (front.empty())
    return ContentKind::TEXT;

  const char first = front.front();

  if (first == '{' || first == '[')
  {
    // Confirm it parses as JSON
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (reader->parse(body.data(), body.data() + body.size(), &root, &errs))
      return ContentKind::JSON;
    return ContentKind::TEXT;  // malformed JSON → treat as text
  }

  if (first == '<')
    return ContentKind::XML;

  return looks_like_text(body) ? ContentKind::TEXT : ContentKind::BINARY;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ContentKind detect_content_kind(const std::string& content_type, const std::string& body)
{
  if (!content_type.empty())
  {
    ContentKind k = kind_from_header(content_type);
    if (k != ContentKind::UNKNOWN)
      return k;

    // "application/octet-stream" or similar – fall through to sniffing
  }
  return sniff_body(body);
}

// ---------------------------------------------------------------------------

std::pair<std::string, std::string> format_for_diff(ContentKind kind, const std::string& body)
{
  switch (kind)
  {
    case ContentKind::TEXT:
      return {body, {}};

    case ContentKind::JSON:
    {
      Json::Value j;
      Json::CharReaderBuilder builder;
      std::string errs;
      std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
      if (reader->parse(body.data(), body.data() + body.size(), &j, &errs))
      {
        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "  ";
        return {Json::writeString(writerBuilder, j), {}};
      }
      else
      {
        // Fall back to raw text so the user can still see the content
        return {body, std::string("JSON parse error: ") + errs};
      }
    }

    case ContentKind::XML:
    {
      tinyxml2::XMLDocument doc;
      if (doc.Parse(body.c_str()) != tinyxml2::XML_SUCCESS)
        return {body, std::string("XML parse error: ") + (doc.ErrorStr() ? doc.ErrorStr() : "?")};

      tinyxml2::XMLPrinter printer;
      doc.Print(&printer);
      return {printer.CStr(), {}};
    }

    case ContentKind::IMAGE:
    case ContentKind::BINARY:
    case ContentKind::UNKNOWN:
      // Caller should use byte-level comparison; no text to diff.
      return {{}, {}};
  }
  return {{}, {}};
}

// ---------------------------------------------------------------------------

const char* content_kind_label(ContentKind kind)
{
  switch (kind)
  {
    case ContentKind::UNKNOWN: return "unknown";
    case ContentKind::BINARY:  return "binary";
    case ContentKind::IMAGE:   return "image";
    case ContentKind::TEXT:    return "text";
    case ContentKind::JSON:    return "JSON";
    case ContentKind::XML:     return "XML";
  }
  return "?";
}
