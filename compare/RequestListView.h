#pragma once
#include "Types.h"

#include <gtkmm/liststore.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treeview.h>

#include <sigc++/sigc++.h>

#include <vector>

/**
 * The top pane: a scrollable TreeView listing every request and its current
 * status.  Owns the ListStore and column model.
 *
 * Emits signal_index_selected(int) whenever the selection changes.  The
 * argument is the original query index (-1 when nothing is selected).
 */
class RequestListView : public Gtk::ScrolledWindow
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

  // Fires when the selected row changes.  Argument is the query index, or -1.
  sigc::signal<void(int)>& signal_index_selected() { return sig_selected_; }

  static Glib::ustring status_text(CompareStatus s);

 private:
  void on_selection_changed_internal();

  struct Columns : public Gtk::TreeModel::ColumnRecord
  {
    Columns()
    {
      add(col_number);
      add(col_index);
      add(col_status);
      add(col_psnr);
      add(col_time);
      add(col_request);
    }
    Gtk::TreeModelColumn<int> col_number;
    Gtk::TreeModelColumn<int> col_index;
    Gtk::TreeModelColumn<Glib::ustring> col_status;
    Gtk::TreeModelColumn<Glib::ustring> col_psnr;
    Gtk::TreeModelColumn<Glib::ustring> col_time;
    Gtk::TreeModelColumn<Glib::ustring> col_request;
  };

  Columns columns_;
  Glib::RefPtr<Gtk::ListStore> store_;
  Gtk::TreeView view_;

  sigc::signal<void(int)> sig_selected_;
};
