#pragma once
#include "EditQueryDialog.h"
#include "Types.h"

#include <gtkmm/box.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/menu.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/treemodelfilter.h>
#include <gtkmm/treeview.h>

#include <sigc++/sigc++.h>

#include <vector>

/**
 * The top pane: a filter bar plus a scrollable TreeView listing every request
 * and its current status.  Owns the ListStore, a TreeModelFilter, and the
 * column model.
 *
 * Three independent filter axes:
 *   Content type : All | Text | Image  (+ optional PSNR threshold for Image)
 *   Status       : All | Equal | Different | Error
 *   HTTP status  : All | 2XX both | Same | Different
 *
 * Emits signal_index_selected(int) whenever the selection changes.  The
 * argument is the original query index (-1 when nothing is selected).
 */
class RequestListView : public Gtk::Box
{
 public:
  RequestListView();

  // Rebuild the list from a fresh batch of queries.  All rows start as
  // PENDING.
  void populate(const std::vector<QueryInfo>& queries);

  // Reset every existing row to the PENDING status label without rebuilding.
  void reset_to_pending();

  // Update the status cell of the row whose index matches result.index.
  void update_status(const CompareResult& result);

  // Empty the list.
  void clear();

  // Index of the currently selected row, or -1 if no selection.
  int selected_index();

  // Original (URL-encoded) request strings of every row currently visible
  // through the active filter, in display order.  Useful for exporting a
  // filtered subset back to a request-list file.
  std::vector<std::string> visible_request_strings() const;

  // Original query indices (col_index) of every row currently visible
  // through the active filter, in display order.  Use when you need to
  // map back into the caller's queries vector (e.g. to preserve fields
  // beyond the request string).
  std::vector<int> visible_indices() const;

  // Fires when the selected row changes.  Argument is the query index, or -1.
  sigc::signal<void(int)>& signal_index_selected() { return sig_selected_; }

  // Which server(s) the user wants the inspector dialog to talk to.
  enum class InspectTarget { Both, Server1, Server2 };

  // Fires when the user picks one of the "Send request…" items from the
  // context menu.  Arguments: query index, and the chosen target.
  sigc::signal<void(int, InspectTarget)>& signal_inspect_requested() { return sig_inspect_; }

  // What the user wants done with the edited query.
  enum class EditAction { Replace, AddAfter };

  // Fires after the user closes the Edit Query dialog with Replace or Add After.
  // Arguments: original query index, new URL-encoded request string, action.
  sigc::signal<void(int, std::string, EditAction)>& signal_query_edited()
  {
    return sig_query_edited_;
  }

  static Glib::ustring status_text(CompareStatus s);

 private:
  void on_selection_changed_internal();
  bool on_button_press(GdkEventButton* event);
  void on_copy_decoded();
  void on_copy_encoded();
  void on_edit_query();
  void emit_inspect(InspectTarget target);
  void on_filter_changed();
  bool filter_func(const Gtk::TreeModel::const_iterator& iter);

  struct Columns : public Gtk::TreeModel::ColumnRecord
  {
    Columns()
    {
      add(col_number);
      add(col_index);
      add(col_status);
      add(col_http);
      add(col_psnr);
      add(col_size);
      add(col_time);
      add(col_request);
      // Hidden columns
      add(col_request_raw);
      add(col_raw_status);
      add(col_is_image);
      add(col_psnr_val);
      add(col_http_code1);
      add(col_http_code2);
    }
    Gtk::TreeModelColumn<int> col_number;
    Gtk::TreeModelColumn<int> col_index;
    Gtk::TreeModelColumn<Glib::ustring> col_status;
    Gtk::TreeModelColumn<Glib::ustring> col_http;
    Gtk::TreeModelColumn<Glib::ustring> col_psnr;
    Gtk::TreeModelColumn<Glib::ustring> col_size;
    Gtk::TreeModelColumn<Glib::ustring> col_time;
    Gtk::TreeModelColumn<Glib::ustring> col_request;
    // Hidden
    Gtk::TreeModelColumn<Glib::ustring> col_request_raw;  // original URL-encoded
    Gtk::TreeModelColumn<int>    col_raw_status;  // CompareStatus as int
    Gtk::TreeModelColumn<bool>   col_is_image;
    Gtk::TreeModelColumn<double> col_psnr_val;
    Gtk::TreeModelColumn<int>    col_http_code1;
    Gtk::TreeModelColumn<int>    col_http_code2;
  };

  Columns columns_;
  Glib::RefPtr<Gtk::ListStore> store_;
  Glib::RefPtr<Gtk::TreeModelFilter> filter_;
  Gtk::TreeView view_;
  Gtk::Menu context_menu_;

  // Filter bar
  Gtk::Box filter_bar_{Gtk::ORIENTATION_HORIZONTAL, 6};
  Gtk::Label lbl_content_{"Content:"};
  Gtk::ComboBoxText cb_content_;
  Gtk::Label lbl_status_{"Status:"};
  Gtk::ComboBoxText cb_status_;
  Gtk::Label lbl_http_{"HTTP:"};
  Gtk::ComboBoxText cb_http_;
  Gtk::Label lbl_psnr_{"Max PSNR (dB):"};
  Gtk::SpinButton spin_psnr_;

  Gtk::ScrolledWindow scroll_;

  sigc::signal<void(int)>                       sig_selected_;
  sigc::signal<void(int, InspectTarget)>        sig_inspect_;
  sigc::signal<void(int, std::string, EditAction)> sig_query_edited_;
};
