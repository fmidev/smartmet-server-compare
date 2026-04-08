#pragma once
#include "CompareRunner.h"
#include "DiffView.h"
#include "QueryFetcher.h"
#include "Settings.h"
#include "Types.h"

#include <glibmm/dispatcher.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/paned.h>
#include <gtkmm/progressbar.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/statusbar.h>
#include <gtkmm/treeview.h>
#include <gtkmm/window.h>

#include <mutex>
#include <thread>
#include <utility>
#include <vector>

class MainWindow : public Gtk::Window
{
 public:
  MainWindow();

 private:
  // ----- UI construction -----
  void build_ui();
  void setup_request_list();

  // Populate a combo from history and pre-fill the entry with the most recent value.
  void load_combo(Gtk::ComboBoxText& combo, const std::string& key);

  // Add combo's current text to history and repopulate the dropdown.
  void save_combo(Gtk::ComboBoxText& combo, const std::string& key);

  // Convenience: get text from a ComboBoxText-with-entry.
  static std::string combo_text(const Gtk::ComboBoxText& combo);

  // Set placeholder text and width on the embedded Entry.
  static void setup_combo_entry(Gtk::ComboBoxText& combo,
                                const char* placeholder,
                                int width_chars);

  // ----- Signal handlers -----
  void on_fetch_clicked();
  void on_load_file_clicked();
  void on_compare_clicked();
  void on_stop_clicked();
  void on_selection_changed();

  // ----- Callbacks from background threads (called on main thread) -----
  void on_fetch_dispatch();
  void on_queries_fetched(std::vector<QueryInfo> queries);
  void on_compare_result(CompareResult result);
  void on_compare_done();

  // ----- Helpers -----
  void populate_list(const std::vector<QueryInfo>& queries);
  void update_row(const CompareResult& result);
  void show_result_in_diff(const CompareResult& result);
  void set_status(const Glib::ustring& msg);
  void set_buttons_running(bool running);

  static Glib::ustring status_text(CompareStatus s);

  // ----- Widgets -----
  Gtk::Box main_box_{Gtk::ORIENTATION_VERTICAL, 0};

  // Row 1: source server
  Gtk::Box row1_{Gtk::ORIENTATION_HORIZONTAL, 6};
  Gtk::Label lbl_source_{"Source server:"};
  Gtk::ComboBoxText ent_source_{true};   // true = has_entry
  
  Gtk::Label lbl_max_size_{"Max size (MB):"};
  Gtk::SpinButton spin_max_size_;
  Gtk::Label lbl_prefix_{"Prefix filter:"};
  Gtk::ComboBoxText ent_prefix_{true};
  Gtk::Label lbl_minutes_{"Minutes:"};
  Gtk::SpinButton spin_minutes_;
  Gtk::Button btn_fetch_{"Fetch queries"};
  Gtk::Button btn_load_file_{"Load from file…"};

  // Row 2: target servers
  Gtk::Box row2_{Gtk::ORIENTATION_HORIZONTAL, 6};
  Gtk::Label lbl_srv1_{"Server 1:"};
  Gtk::ComboBoxText ent_srv1_{true};
  Gtk::Label lbl_srv2_{"Server 2:"};
  Gtk::ComboBoxText ent_srv2_{true};
  Gtk::Label lbl_concurrent_{"Max concurrent:"};
  Gtk::SpinButton spin_concurrent_;
  Gtk::Button btn_compare_{"Compare all"};
  Gtk::Button btn_stop_{"Stop"};

  Gtk::Separator sep_;

  // Vertical split: list (top) | diff (bottom)
  Gtk::Paned vpaned_{Gtk::ORIENTATION_VERTICAL};

  Gtk::ScrolledWindow list_scroll_;
  Gtk::TreeView list_view_;

  DiffView diff_view_;

  // Bottom status bar
  Gtk::Box status_box_{Gtk::ORIENTATION_HORIZONTAL, 6};
  Gtk::ProgressBar progress_;
  Gtk::Statusbar statusbar_;

  // ----- TreeView model -----
  struct Columns : public Gtk::TreeModel::ColumnRecord
  {
    Columns()
    {
      add(col_number);
      add(col_index);
      add(col_status);
      add(col_time);
      add(col_request);
    }
    Gtk::TreeModelColumn<int> col_number;
    Gtk::TreeModelColumn<int> col_index;
    Gtk::TreeModelColumn<Glib::ustring> col_status;
    Gtk::TreeModelColumn<Glib::ustring> col_time;
    Gtk::TreeModelColumn<Glib::ustring> col_request;
  };
  Columns columns_;
  Glib::RefPtr<Gtk::ListStore> list_store_;

  // ----- Application state -----
  std::vector<QueryInfo> queries_;
  std::vector<CompareResult> results_;

  CompareRunner runner_;

  // Dispatcher + mutex for fetch thread → main thread
  Glib::Dispatcher fetch_dispatcher_;
  std::mutex fetch_mutex_;
  std::pair<std::vector<QueryInfo>, std::string> fetch_result_;
  QueryFetcher fetcher_;

  int total_queries_{0};
  int done_queries_{0};

  // ----- Persistent settings -----
  Settings settings_;
};
