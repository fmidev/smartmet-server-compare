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
#include <sigc++/connection.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
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
  ~MainWindow() override;

 private:
  // ----- InputBar signal handlers -----
  void on_fetch_requested();
  void on_load_file_requested();
  void on_save_file_requested();
  void on_compare_requested();
  void on_rerun_filtered_requested();
  void on_stop_requested();

  // ----- RequestListView selection -----
  void on_row_selected(int index);
  void on_inspect_requested(int index, RequestListView::InspectTarget target);

  // Application-level keyboard shortcuts: Ctrl+O / Ctrl+F / F5 / Escape /
  // Ctrl+Q.  Overridden rather than connected so shortcuts fire regardless
  // of which widget currently has focus.
  bool on_key_press_event(GdkEventKey* event) override;

  // ----- Background-worker callbacks (main thread) -----
  void on_fetch_dispatch();
  void on_queries_fetched(std::vector<QueryInfo> queries);
  void on_compare_result(CompareResult result);
  void on_compare_done();

  // ----- Helpers -----
  void show_result(int index);

  // Orchestrate the asynchronous result-display pipeline: debounces rapid
  // selection changes, runs ResultPanel::prepare_async on a worker thread,
  // and posts the outcome back to the main loop via `show_dispatcher_`.
  // `debounce_ms = 0` skips the delay (used by on_compare_result which is
  // already event-driven rather than user-scrolling-driven).
  void schedule_show(int index, int debounce_ms);
  void kick_show_worker(int index);
  void on_show_dispatch();
  void cancel_pending_show();

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

  // ----- Async show pipeline -----
  // `current_show_idx_` is the latest user-requested index; show workers
  // that come back with a different idx have their result dropped.  The
  // cancel token (shared with the in-flight worker, if any) is flipped to
  // true when a new request arrives so the worker can bail out early.
  sigc::connection                 show_debounce_;
  std::shared_ptr<std::atomic<bool>> show_cancel_;
  std::thread                      show_worker_;
  int                              current_show_idx_{-1};

  Glib::Dispatcher show_dispatcher_;
  std::mutex       show_mutex_;
  struct ShowReady
  {
    int                       idx    = -1;
    ResultViewer*             viewer = nullptr;
    std::shared_ptr<void>     prepared;
    std::string               server1;
    std::string               server2;
  };
  ShowReady show_ready_;
};
