#include "ContentHandler.h"

#include <json/json.h>
#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

  if (has("image/svg"))
    return ContentKind::SVG;

  if (has("xml") || has("xhtml") || has("gml") || has("kml") || has("wfs") || has("wms"))
    return ContentKind::XML;

  if (ct.find("text/html") != std::string::npos)
    return ContentKind::XML;  // treat HTML as XML for pretty-printing

  if (ct.find("text/") == 0)
    return ContentKind::TEXT;

  if (has("application/pdf") || has("application/x-pdf"))
    return ContentKind::PDF;

  if (has("image/"))
    return ContentKind::IMAGE;

  return ContentKind::UNKNOWN;
}

// Cheap test: does the body look like a SmartMet "serial" (PHP-serialize)
// payload?  The grammar always starts with a type tag followed by ':' (or
// ';' for null).  A stricter structural check happens inside the parser;
// this is just a fast pre-filter so plain text isn't mis-classified.
static bool looks_like_serial(const std::string& body)
{
  if (body.size() < 4) return false;
  const char c = body[0];
  if (c != 'a' && c != 's' && c != 'i' && c != 'd' && c != 'b')
    return false;
  if (body[1] != ':') return false;
  if (!std::isdigit(static_cast<unsigned char>(body[2])) && body[2] != '-')
    return false;

  // SmartMet's output is always an outer array.  Require that specifically
  // so we don't hijack arbitrary text that happens to start with "i:…".
  if (c != 'a') return false;

  std::size_t pos = 2;
  while (pos < body.size() && std::isdigit(static_cast<unsigned char>(body[pos])))
    ++pos;
  return pos > 2 && pos + 1 < body.size() && body[pos] == ':' && body[pos + 1] == '{';
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

  // PDF magic
  if (body.size() >= 5 && body.substr(0, 5) == "%PDF-")
    return ContentKind::PDF;

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
  {
    // Distinguish SVG from generic XML by looking for an <svg root element.
    if (front.find("<svg") != std::string_view::npos)
      return ContentKind::SVG;
    return ContentKind::XML;
  }

  if (looks_like_serial(body))
    return ContentKind::SERIAL;

  return looks_like_text(body) ? ContentKind::TEXT : ContentKind::BINARY;
}

// ---------------------------------------------------------------------------
// SmartMet "serial" (PHP-serialize) pretty-printer
// ---------------------------------------------------------------------------
//
// The format is emitted by
//   smartmet-library-spine/spine/SerialFormatter.cpp
// and is essentially a subset of PHP's serialize():
//   N;                       – null
//   b:<0|1>;                 – boolean
//   i:<int>;                 – integer
//   d:<float>;               – double
//   s:<len>:"<len bytes>";   – string (length in bytes, raw content, no escaping)
//   a:<n>:{ key value … }    – associative array of n key/value pairs
//
// We parse into a small AST and emit a newline-separated rendering where
// each array entry goes on its own line.  The format stays byte-accurate
// so that round-tripping remains possible if ever needed.

namespace {

struct SerialNode
{
  enum Kind { NIL, BOOL, INT, FLOAT, STRING, ARRAY } kind = NIL;
  bool        b = false;
  std::string str;  // INT/FLOAT keep the original numeric text; STRING holds the bytes
  std::vector<std::pair<SerialNode, SerialNode>> items;
};

class SerialParser
{
 public:
  explicit SerialParser(const std::string& src) : src_(src) {}

  bool parse(SerialNode& out)
  {
    return parse_value(out) && pos_ == src_.size();
  }

  std::size_t position() const { return pos_; }

 private:
  bool parse_value(SerialNode& out)
  {
    if (pos_ >= src_.size()) return false;
    switch (src_[pos_])
    {
      case 'N':
        if (pos_ + 1 >= src_.size() || src_[pos_ + 1] != ';') return false;
        out.kind = SerialNode::NIL;
        pos_ += 2;
        return true;

      case 'b':
        if (pos_ + 3 >= src_.size() || src_[pos_ + 1] != ':' || src_[pos_ + 3] != ';')
          return false;
        out.kind = SerialNode::BOOL;
        out.b = (src_[pos_ + 2] == '1');
        pos_ += 4;
        return true;

      case 'i':
        return parse_scalar(out, SerialNode::INT);

      case 'd':
        return parse_scalar(out, SerialNode::FLOAT);

      case 's':
        return parse_string(out);

      case 'a':
        return parse_array(out);

      default:
        return false;
    }
  }

  // "<tag>:<digits and minus and dot>;"  – i and d share this shape
  bool parse_scalar(SerialNode& out, SerialNode::Kind kind)
  {
    if (pos_ + 1 >= src_.size() || src_[pos_ + 1] != ':') return false;
    pos_ += 2;
    const std::size_t end = src_.find(';', pos_);
    if (end == std::string::npos) return false;
    out.kind = kind;
    out.str.assign(src_, pos_, end - pos_);
    pos_ = end + 1;
    return true;
  }

  bool parse_string(SerialNode& out)
  {
    if (pos_ + 1 >= src_.size() || src_[pos_ + 1] != ':') return false;
    pos_ += 2;
    const std::size_t colon = src_.find(':', pos_);
    if (colon == std::string::npos) return false;
    const std::string len_text(src_, pos_, colon - pos_);
    std::size_t n;
    try { n = static_cast<std::size_t>(std::stoull(len_text)); }
    catch (...) { return false; }

    pos_ = colon + 1;
    // Expect: "<n bytes>";
    if (pos_ >= src_.size() || src_[pos_] != '"') return false;
    ++pos_;
    if (pos_ + n + 2 > src_.size()) return false;
    if (src_[pos_ + n] != '"' || src_[pos_ + n + 1] != ';') return false;

    out.kind = SerialNode::STRING;
    out.str.assign(src_, pos_, n);
    pos_ += n + 2;  // trailing "; consumed
    return true;
  }

  bool parse_array(SerialNode& out)
  {
    if (pos_ + 1 >= src_.size() || src_[pos_ + 1] != ':') return false;
    pos_ += 2;
    const std::size_t colon = src_.find(':', pos_);
    if (colon == std::string::npos) return false;
    std::size_t n;
    try { n = static_cast<std::size_t>(std::stoull(std::string(src_, pos_, colon - pos_))); }
    catch (...) { return false; }

    pos_ = colon + 1;
    if (pos_ >= src_.size() || src_[pos_] != '{') return false;
    ++pos_;

    out.kind = SerialNode::ARRAY;
    out.items.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
      SerialNode key, value;
      if (!parse_value(key)) return false;
      if (!parse_value(value)) return false;
      out.items.emplace_back(std::move(key), std::move(value));
    }
    if (pos_ >= src_.size() || src_[pos_] != '}') return false;
    ++pos_;
    return true;
  }

  const std::string& src_;
  std::size_t        pos_ = 0;
};

// Write a scalar value to `out` in canonical serial syntax.
void emit_scalar(const SerialNode& n, std::string& out)
{
  switch (n.kind)
  {
    case SerialNode::NIL:   out += "N;"; return;
    case SerialNode::BOOL:  out += "b:"; out += (n.b ? '1' : '0'); out += ';'; return;
    case SerialNode::INT:   out += "i:"; out += n.str; out += ';'; return;
    case SerialNode::FLOAT: out += "d:"; out += n.str; out += ';'; return;
    case SerialNode::STRING:
      out += "s:"; out += std::to_string(n.str.size()); out += ":\"";
      out += n.str;
      out += "\";";
      return;
    case SerialNode::ARRAY:
      // Should not reach here — arrays go through emit_node which handles
      // indentation.  Fall through to an inline rendering to stay correct.
      out += "a:";
      out += std::to_string(n.items.size());
      out += ":{}";
      return;
  }
}

void emit_node(const SerialNode& n, std::string& out, int depth);

// Write the value part of a key/value pair: if it's an array start it on
// the same line (right after the key) with an opening '{' then indent the
// items; otherwise emit it as a trailing scalar.
void emit_value(const SerialNode& n, std::string& out, int depth)
{
  if (n.kind == SerialNode::ARRAY)
    emit_node(n, out, depth);
  else
    emit_scalar(n, out);
}

void emit_node(const SerialNode& n, std::string& out, int depth)
{
  if (n.kind != SerialNode::ARRAY)
  {
    emit_scalar(n, out);
    return;
  }
  if (n.items.empty())
  {
    out += "a:0:{}";
    return;
  }

  const std::string inner_indent(static_cast<std::size_t>(depth + 1) * 2, ' ');
  const std::string close_indent(static_cast<std::size_t>(depth) * 2, ' ');

  out += "a:";
  out += std::to_string(n.items.size());
  out += ":{\n";

  for (const auto& [key, value] : n.items)
  {
    out += inner_indent;
    emit_scalar(key, out);
    emit_value(value, out, depth + 1);
    out += '\n';
  }

  out += close_indent;
  out += '}';
}

// Top-level pretty-print.  Returns empty string if the input doesn't parse.
std::string pretty_print_serial(const std::string& body)
{
  SerialNode root;
  SerialParser parser(body);
  if (!parser.parse(root))
    return {};

  std::string out;
  out.reserve(body.size() + body.size() / 8);
  emit_node(root, out, 0);
  return out;
}

}  // namespace

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

    case ContentKind::SERIAL:
    {
      std::string pretty = pretty_print_serial(body);
      if (pretty.empty())
        return {body, "Serial parse error"};
      return {std::move(pretty), {}};
    }

    case ContentKind::IMAGE:
    case ContentKind::SVG:
    case ContentKind::PDF:
    case ContentKind::BINARY:
    case ContentKind::UNKNOWN:
      // Caller should use byte-level or image comparison; no text to diff.
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
    case ContentKind::SVG:     return "SVG";
    case ContentKind::PDF:     return "PDF";
    case ContentKind::TEXT:    return "text";
    case ContentKind::JSON:    return "JSON";
    case ContentKind::XML:     return "XML";
    case ContentKind::SERIAL:  return "serial";
  }
  return "?";
}
