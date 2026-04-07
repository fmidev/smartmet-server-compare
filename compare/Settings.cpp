#include "Settings.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct Settings::Impl
{
  nlohmann::json data;
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
    return;

  try
  {
    std::ifstream f(path_);
    impl_->data = nlohmann::json::parse(f);
  }
  catch (...)
  {
    // Corrupt file – start fresh
    impl_->data = nlohmann::json::object();
  }
}

void Settings::save() const
{
  try
  {
    std::filesystem::create_directories(path_.parent_path());
    std::ofstream f(path_);
    f << impl_->data.dump(2) << "\n";
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
  if (!d.contains("history") || !d["history"].contains(key))
    return result;

  for (const auto& item : d["history"][key])
  {
    if (item.is_string())
      result.push_back(item.get<std::string>());
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
  if (!arr.is_array())
    arr = nlohmann::json::array();

  // Remove existing occurrence so we can move it to the front
  for (auto it = arr.begin(); it != arr.end(); ++it)
  {
    if (it->is_string() && it->get<std::string>() == value)
    {
      arr.erase(it);
      break;
    }
  }

  arr.insert(arr.begin(), value);

  if (static_cast<int>(arr.size()) > max_items)
    arr.erase(arr.begin() + max_items, arr.end());

  save();
}

// ---------------------------------------------------------------------------

int Settings::get_int(const std::string& key, int default_val) const
{
  const auto& d = impl_->data;
  if (d.contains("scalars") && d["scalars"].contains(key) && d["scalars"][key].is_number())
    return d["scalars"][key].get<int>();
  return default_val;
}

void Settings::set_int(const std::string& key, int value)
{
  impl_->data["scalars"][key] = value;
  save();
}
