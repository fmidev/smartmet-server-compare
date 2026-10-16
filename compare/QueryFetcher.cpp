#include "QueryFetcher.h"
#include "HttpClient.h"

#include <json/json.h>

#include <cctype>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{
std::string make_admin_url(const std::string& server_url, int minutes)
{
  std::string base = server_url;
  if (!base.empty() && base.back() == '/')
    base.pop_back();
  return base + "/admin?what=lastrequests&format=json&minutes=" +
         std::to_string(minutes);
}
}  // namespace

// ---------------------------------------------------------------------------
// QueryFetcher
// ---------------------------------------------------------------------------

/* static */
std::pair<std::vector<QueryInfo>, std::string> QueryFetcher::fetch(const std::string& server_url,
                                                                    const std::string& prefix,
                                                                    int minutes)
{
  const std::string url = make_admin_url(server_url, minutes);

  HttpClient client(30);
  client.add("admin", url);
  client.execute();

  const auto& resp = client.response("admin");
  if (!resp.error.empty())
    return {{}, "Network error fetching queries: " + resp.error};

  if (resp.status_code != 200)
    return {{}, "Server returned HTTP " + std::to_string(resp.status_code)};

  std::vector<QueryInfo> queries;
  try
  {
    Json::Value j;
    Json::CharReaderBuilder readerBuilder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
    if (!reader->parse(resp.body.data(), resp.body.data() + resp.body.size(), &j, &errs))
      return {{}, "JSON parse error: " + errs};

    if (!j.isArray())
      return {{}, "Expected a JSON array from /admin?what=lastrequests"};

    std::optional<std::string> request_member;

    std::unordered_set<std::string> seen;
    for (const auto& item : j)
    {
      if (!item.isObject())
        continue;
      if (!request_member)
      {
        if (item.isMember("RequestString"))
          request_member = "RequestString";
        else if (item.isMember("URL"))
          request_member = "URL";
        else
          return {{}, "JSON objects do not contain 'RequestString' or 'URL' members"};
      }
      std::string rs = request_member && item.isMember(*request_member) && item[*request_member].isString()
                       ? item[*request_member].asString() : "";
      if (rs.empty())
        continue;
      if (!prefix.empty() && rs.substr(0, prefix.size()) != prefix)
        continue;
      if (!seen.insert(rs).second)
        continue;

      QueryInfo qi;
      qi.request_string = std::move(rs);
      qi.time_utc = item.isMember("Time") && item["Time"].isString()
                    ? item["Time"].asString() : "";
      queries.push_back(std::move(qi));
    }
  }
  catch (const std::exception& e)
  {
    return {{}, std::string("JSON parse error: ") + e.what()};
  }

  return {std::move(queries), {}};
}

namespace
{
// Parse a SmartMet server access-log line of the form
//   192.168.14.21 - - [2026-04-23T10:10:02,479843] "GET /path?… HTTP/1.0" 200 …
// On success returns true and fills `request_out` with the request path +
// query and `time_out` with the first timestamp.  All matching lines are
// imported regardless of status code; the user can weed out failures by
// re-running the comparison.
bool parse_access_log_line(const std::string& line,
                                  std::string& request_out,
                                  std::string& time_out)
{
  // Equivalent to the regular expression
  //   ^\S+\s+\S+\s+\S+\s+\[([^\]]+)\]\s+"(GET|POST|HEAD)\s+(\S+)\s+HTTP/\S+"\s+\d+\b.*$
  // without a regex library
  std::size_t pos = 0;
  const auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
  const auto skip_spaces = [&]()
  {
    const auto start = pos;
    while (pos < line.size() && is_space(line[pos]))
      ++pos;
    return pos > start;
  };
  const auto token = [&]()
  {
    const auto start = pos;
    while (pos < line.size() && !is_space(line[pos]))
      ++pos;
    return line.substr(start, pos - start);
  };

  // remote host, identity and user
  for (int i = 0; i < 3; i++)
    if (token().empty() || !skip_spaces())
      return false;

  // [timestamp]
  if (pos >= line.size() || line[pos] != '[')
    return false;
  const auto close = line.find(']', pos + 1);
  if (close == std::string::npos || close == pos + 1)
    return false;
  const std::string timestamp = line.substr(pos + 1, close - pos - 1);
  pos = close + 1;
  if (!skip_spaces())
    return false;

  // "METHOD request HTTP/x.y"
  if (pos >= line.size() || line[pos] != '"')
    return false;
  ++pos;
  const std::string method = token();
  if (method != "GET" && method != "POST" && method != "HEAD")
    return false;
  if (!skip_spaces())
    return false;
  const std::string request = token();
  if (request.empty() || !skip_spaces())
    return false;
  const std::string protocol = token();
  if (protocol.size() < 7 || protocol.compare(0, 5, "HTTP/") != 0 || protocol.back() != '"')
    return false;

  // status code
  if (!skip_spaces())
    return false;
  const auto digits = pos;
  while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])) != 0)
    ++pos;
  if (pos == digits)
    return false;
  if (pos < line.size() &&
      (std::isalnum(static_cast<unsigned char>(line[pos])) != 0 || line[pos] == '_'))
    return false;

  request_out = request;
  time_out = timestamp;
  return true;
}
}  // namespace

/* static */
FileLoadResult QueryFetcher::fetch_from_file(const std::filesystem::path& path)
{
  std::ifstream f(path);
  if (!f)
    return {{}, "Cannot open file: " + path.string(), {}};

  std::vector<QueryInfo> queries;
  std::vector<std::pair<int, std::string>> problematic_lines;
  std::unordered_set<std::string> seen;
  std::string line;
  int line_number = 0;

  while (std::getline(f, line))
  {
    ++line_number;

    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    // Check for whitespace-only lines
    bool is_whitespace_only = true;
    for (char c : line)
    {
      if (!std::isspace(static_cast<unsigned char>(c)))
      {
        is_whitespace_only = false;
        break;
      }
    }

    if (is_whitespace_only || line.front() == '#')
      continue;

    std::string req;
    std::string time_utc;

    if (line.front() == '/')
    {
      req = std::move(line);
    }
    else if (!parse_access_log_line(line, req, time_utc))
    {
      problematic_lines.emplace_back(line_number,
          "Does not match request path (starts with '/') or access-log format");
      continue;
    }

    if (!seen.insert(req).second)
      continue;

    QueryInfo qi;
    qi.request_string = std::move(req);
    qi.time_utc = std::move(time_utc);
    queries.push_back(std::move(qi));
  }

  return {std::move(queries), {}, std::move(problematic_lines)};
}

void QueryFetcher::fetch_async(const std::string& server_url,
                               const std::string& prefix,
                               int minutes,
                               Callback cb)
{
  if (thread_.joinable())
    thread_.detach();

  thread_ = std::thread(
      [=, cb = std::move(cb)]()
      {
        auto [queries, error] = fetch(server_url, prefix, minutes);
        cb(std::move(queries), std::move(error));
      });
}

QueryFetcher::~QueryFetcher()
{
  if (thread_.joinable())
    thread_.detach();
}
