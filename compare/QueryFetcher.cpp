#include "QueryFetcher.h"
#include "UrlUtils.h"

#include <smartmet/spine/TcpMultiQuery.h>

#include <json/json.h>

#include <fstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string make_admin_path(int minutes)
{
  return "/admin?what=lastrequests&format=json&minutes=" + std::to_string(minutes);
}

// Strip the HTTP response headers and return only the body.
static std::string strip_http_headers(const std::string& raw)
{
  const std::string sep = "\r\n\r\n";
  auto pos = raw.find(sep);
  if (pos != std::string::npos)
    return raw.substr(pos + sep.size());

  // Fall back: some servers use bare LF
  const std::string sep2 = "\n\n";
  pos = raw.find(sep2);
  if (pos != std::string::npos)
    return raw.substr(pos + sep2.size());

  return raw;
}

// ---------------------------------------------------------------------------
// QueryFetcher
// ---------------------------------------------------------------------------

/* static */
std::pair<std::vector<QueryInfo>, std::string> QueryFetcher::fetch(const std::string& server_url,
                                                                    const std::string& prefix,
                                                                    int minutes)
{
  auto addr = parse_server_url(server_url);
  if (!addr)
    return {{}, "Cannot parse server URL: " + server_url};

  const std::string path = make_admin_path(minutes);
  const std::string request = build_http_request(addr->host, path);

  SmartMet::Spine::TcpMultiQuery query(30);
  query.add_query("admin", addr->host, addr->port, request);
  query.execute();

  const auto& resp = query["admin"];
  if (resp.error_code)
    return {{}, "Network error fetching queries: " + resp.error_desc};

  const std::string body = strip_http_headers(resp.body);

  std::vector<QueryInfo> queries;
  try
  {
    Json::Value j;
    Json::CharReaderBuilder readerBuilder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
    if (!reader->parse(body.data(), body.data() + body.size(), &j, &errs))
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
      if (!seen.insert(rs).second)  // duplicate – skip
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
    // Strip trailing carriage-return (Windows line endings)
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    // Skip blank lines and comments
    if (line.empty() || line.front() == '#')
      continue;

    if (!seen.insert(line).second)  // duplicate
      continue;

    QueryInfo qi;
    qi.request_string = std::move(line);
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
