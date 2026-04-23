#include "QueryFetcher.h"
#include "HttpClient.h"

#include <json/json.h>

#include <fstream>
#include <regex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string make_admin_url(const std::string& server_url, int minutes)
{
  std::string base = server_url;
  if (!base.empty() && base.back() == '/')
    base.pop_back();
  return base + "/admin?what=lastrequests&format=json&minutes=" +
         std::to_string(minutes);
}

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

    std::unordered_set<std::string> seen;
    for (const auto& item : j)
    {
      std::string rs = item.isMember("RequestString") && item["RequestString"].isString()
                       ? item["RequestString"].asString() : "";
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

// Parse a SmartMet server access-log line of the form
//   192.168.14.21 - - [2026-04-23T10:10:02,479843] "GET /path?… HTTP/1.0" 200 …
// On success returns true and fills `request_out` with the request path +
// query and `time_out` with the first timestamp.  All matching lines are
// imported regardless of status code; the user can weed out failures by
// re-running the comparison.
static bool parse_access_log_line(const std::string& line,
                                  std::string& request_out,
                                  std::string& time_out)
{
  static const std::regex re(
      R"(^\S+\s+\S+\s+\S+\s+\[([^\]]+)\]\s+\"(GET|POST|HEAD)\s+(\S+)\s+HTTP/\S+\"\s+\d+\b.*$)");

  std::smatch m;
  if (!std::regex_match(line, m, re))
    return false;

  request_out = m[3].str();
  time_out = m[1].str();
  return true;
}

/* static */
std::pair<std::vector<QueryInfo>, std::string> QueryFetcher::fetch_from_file(
    const std::filesystem::path& path)
{
  std::ifstream f(path);
  if (!f)
    return {{}, "Cannot open file: " + path.string()};

  std::vector<QueryInfo> queries;
  std::unordered_set<std::string> seen;
  std::string line;

  while (std::getline(f, line))
  {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    if (line.empty() || line.front() == '#')
      continue;

    std::string req;
    std::string time_utc;

    if (line.front() == '/')
    {
      req = std::move(line);
    }
    else if (!parse_access_log_line(line, req, time_utc))
    {
      continue;
    }

    if (!seen.insert(req).second)
      continue;

    QueryInfo qi;
    qi.request_string = std::move(req);
    qi.time_utc = std::move(time_utc);
    queries.push_back(std::move(qi));
  }

  return {std::move(queries), {}};
}

void QueryFetcher::fetch_async(std::string server_url,
                               std::string prefix,
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
