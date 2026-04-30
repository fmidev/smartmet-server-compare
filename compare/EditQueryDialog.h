#pragma once
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

#include <string>
#include <vector>

/**
 * Modal dialog for editing a single URL-encoded query.
 *
 * The request string is split into its path component and query parameters;
 * each parameter is shown as an editable key / value pair.  The user can
 * add or remove parameters freely and see a live URL preview at the bottom.
 *
 * On close, check run()'s response id:
 *   RESPONSE_REPLACE   – overwrite the original entry in the query list
 *   RESPONSE_ADD_AFTER – insert a new entry immediately after the original
 *   anything else       – discard all changes
 *
 * Call get_result_request() to retrieve the reconstructed URL-encoded string.
 */
class EditQueryDialog : public Gtk::Dialog
{
 public:
  static const int RESPONSE_REPLACE   = 1;
  static const int RESPONSE_ADD_AFTER = 2;

  EditQueryDialog(Gtk::Window& parent, const std::string& request_string);

  // Reconstructed, URL-encoded request string from the current editor state.
  std::string get_result_request() const;

 private:
  struct ParamRow
  {
    Gtk::Entry* key_entry;
    Gtk::Entry* val_entry;
    Gtk::Box*   widget;
  };

  void parse_and_populate(const std::string& request);
  void add_param_row(const std::string& key = {}, const std::string& val = {});
  void remove_param_row(Gtk::Box* row_widget);
  void update_preview();
  std::string build_request() const;

  Gtk::Entry          path_entry_;
  Gtk::Box            params_box_{Gtk::ORIENTATION_VERTICAL, 2};
  Gtk::ScrolledWindow params_scroll_;
  Gtk::Entry          preview_entry_;

  std::vector<ParamRow> param_rows_;
};
