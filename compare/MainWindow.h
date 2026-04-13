#pragma once
#include "CompareRunner.h"
#include "InputBar.h"
#include "QueryFetcher.h"
#include "RequestListView.h"
#include "ResultPanel.h"
#include "Settings.h"
#include "StatusPanel.h"
#include "Types.h"

#include <glibmm/dispatcher.h>
#include <gtkmm/box.h>
#include <gtkmm/paned.h>
#include <gtkmm/separator.h>
#include <gtkmm/window.h>

#include <mutex>
#include <utility>
#include <vector>

/**
 * Thin coordinator that wires together the input bar, request list, result
 * panel and status bar with the QueryFetcher and CompareRunner background
 * workers.  Holds no UI construction logic of its own.
 */
class MainWindow : public Gtk::Window
{
 public:
  MainWindow();

 private:
  // ----- InputBar signal handlers -----
  void on_fetch_requested();
  void on_load_file_requested();
  void on_compare_requested();
  void on_stop_requested();

  // ----- RequestListView selection -----
  void on_row_selected(int index);

  // ----- Background-worker callbacks (main thread) -----
  void on_fetch_dispatch();
  void on_queries_fetched(std::vector<QueryInfo> queries);
  void on_compare_result(CompareResult result);
  void on_compare_done();

  // ----- Helpers -----
  void show_result(int index);

  // ----- Sub-widgets -----
  Gtk::Box main_box_{Gtk::ORIENTATION_VERTICAL, 0};
  Settings        settings_;
  InputBar        input_bar_{settings_};
  Gtk::Separator  sep_;
  Gtk::Paned      vpaned_{Gtk::ORIENTATION_VERTICAL};
  RequestListView list_view_;
  ResultPanel     result_panel_;
  StatusPanel     status_panel_;

  // ----- Application state -----
  std::vector<QueryInfo>     queries_;
  std::vector<CompareResult> results_;

  CompareRunner runner_;
  QueryFetcher  fetcher_;

  // Fetch thread → main thread
  Glib::Dispatcher fetch_dispatcher_;
  std::mutex       fetch_mutex_;
  std::pair<std::vector<QueryInfo>, std::string> fetch_result_;

  int total_queries_{0};
  int done_queries_{0};
};
