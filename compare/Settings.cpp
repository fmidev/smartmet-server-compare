#include "Settings.h"

#include <json/json.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct Settings::Impl
{
  Json::Value data;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::filesystem::path settings_path()
{
  const char* home = std::getenv("HOME");
  std::filesystem::path base = home ? home : ".";
  return base / ".local" / "share" / "smartmet-server-compare" / "history.json";
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

Settings::Settings() : path_(settings_path()), impl_(std::make_unique<Impl>())
{
  load();
}

Settings::~Settings() = default;

void Settings::load()
{
  if (!std::filesystem::exists(path_))
  {
    impl_->data = Json::Value(Json::objectValue);
    return;
  }

  try
  {
    std::ifstream f(path_);
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, f, &impl_->data, &errs))
    {
      impl_->data = Json::Value(Json::objectValue);
    }
  }
  catch (...)
  {
    // Corrupt file – start fresh
    impl_->data = Json::Value(Json::objectValue);
  }
}

void Settings::save() const
{
  try
  {
    std::filesystem::create_directories(path_.parent_path());
    std::ofstream f(path_);
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(impl_->data, &f);
    f << "\n";
  }
  catch (...)
  {
    // Ignore write errors silently
  }
}

// ---------------------------------------------------------------------------

std::vector<std::string> Settings::history(const std::string& key) const
{
  std::vector<std::string> result;
  const auto& d = impl_->data;
  if (!d.isMember("history") || !d["history"].isMember(key))
    return result;

  const auto& arr = d["history"][key];
  if (!arr.isArray())
    return result;

  for (const auto& item : arr)
  {
    if (item.isString())
      result.push_back(item.asString());
  }
  return result;
}

void Settings::add_to_history(const std::string& key,
                               const std::string& value,
                               int max_items)
{
  if (value.empty())
    return;

  auto& arr = impl_->data["history"][key];
  if (!arr.isArray())
    arr = Json::Value(Json::arrayValue);

  Json::Value new_arr(Json::arrayValue);
  new_arr.append(value);
  int count = 1;

  for (const auto& item : arr)
  {
    if (item.isString() && item.asString() == value)
      continue;
    if (count < max_items)
    {
      new_arr.append(item);
      count++;
    }
  }

  impl_->data["history"][key] = new_arr;

  save();
}

// ---------------------------------------------------------------------------

int Settings::get_int(const std::string& key, int default_val) const
{
  const auto& d = impl_->data;
  if (d.isMember("scalars") && d["scalars"].isMember(key) && d["scalars"][key].isNumeric())
    return d["scalars"][key].asInt();
  return default_val;
}

void Settings::set_int(const std::string& key, int value)
{
  if (!impl_->data.isMember("scalars") || !impl_->data["scalars"].isObject())
    impl_->data["scalars"] = Json::Value(Json::objectValue);
  impl_->data["scalars"][key] = value;
  save();
}
