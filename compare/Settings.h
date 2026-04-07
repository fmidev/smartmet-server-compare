#pragma once
#include <filesystem>
#include <string>
#include <vector>

/**
 * Persistent application settings stored as JSON under
 *   $HOME/.local/share/smartmet-server-compare/history.json
 *
 * Two kinds of data are stored:
 *
 *   history(key)          – ordered list of strings (most-recent first).
 *                           add_to_history() deduplicates and caps the list.
 *
 *   get_int / set_int     – simple integer scalars (spinner values etc.).
 *
 * Changes are written to disk immediately by add_to_history() / set_int().
 */
class Settings
{
 public:
  Settings();
  ~Settings();

  // Returns stored history for key (most-recent first).  Empty if absent.
  std::vector<std::string> history(const std::string& key) const;

  // Prepend value to the history for key (move to front if already present),
  // trim list to max_items, then save.
  void add_to_history(const std::string& key,
                      const std::string& value,
                      int max_items = 20);

  int  get_int(const std::string& key, int default_val) const;
  void set_int(const std::string& key, int value);

  // Flush current state to disk.
  void save() const;

 private:
  void load();

  std::filesystem::path path_;

  // Internal storage – parsed JSON kept in memory as a flat map-of-variants.
  // Using two separate maps avoids pulling nlohmann into the header.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
