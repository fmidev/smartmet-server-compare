#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
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
 *   history_pairs(key)    – same, but each entry is a pair of strings that
 *                           belong together (e.g. fetch prefix + regex).
 *
 *   get_int / set_int     – simple integer scalars (spinner values etc.).
 *
 * Changes are written to disk immediately by add_to_history() /
 * add_to_history_pairs() / set_int().
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

  // Replace the whole history for key, then save.
  void set_history(const std::string& key,
                   const std::vector<std::string>& values);

  // Pair-valued history, stored as two-element JSON arrays.  Semantics as
  // for history() / add_to_history(); a pair is stored unless both strings
  // are empty.
  using Pair = std::pair<std::string, std::string>;
  std::vector<Pair> history_pairs(const std::string& key) const;
  void add_to_history_pairs(const std::string& key,
                            const Pair& value,
                            int max_items = 20);

  int  get_int(const std::string& key, int default_val) const;
  void set_int(const std::string& key, int value);

  // Flush current state to disk.
  void save() const;

 private:
  void load();

  std::filesystem::path path_;

  // Internal storage – parsed JSON kept in memory as a flat map-of-variants.
  // Using two separate maps avoids pulling jsoncpp into the header.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
