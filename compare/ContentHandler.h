#pragma once
#include "Types.h"

#include <string>
#include <utility>

/**
 * Content-type detection and pre-diff formatting.
 *
 * detect_content_kind()
 *   Determines the semantic type from the Content-Type header and, when that
 *   is absent or generic, by sniffing the body bytes.
 *
 * format_for_diff()
 *   Returns a normalised, human-readable rendering suitable for line-by-line
 *   text diffing:
 *     TEXT  → body as-is
 *     JSON  → pretty-printed with 2-space indent (via nlohmann/json)
 *     XML   → pretty-printed with tinyxml2
 *     IMAGE / BINARY → empty string  (caller should use byte comparison)
 *
 *   On parse failure the raw body is returned unchanged so the user still
 *   sees something, and the error string is set.
 *
 * content_kind_label()
 *   Short human-readable label for display in the UI.
 */

ContentKind detect_content_kind(const std::string& content_type, const std::string& body);

std::pair<std::string /*formatted*/, std::string /*error*/>
format_for_diff(ContentKind kind, const std::string& body);

const char* content_kind_label(ContentKind kind);
