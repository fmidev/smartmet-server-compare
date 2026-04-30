#include "MainWindow.h"

#include "ImageDiffViewer.h"
#include "InspectDialog.h"
#include "TextDiffViewer.h"

#include <glibmm/main.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/settings.h>

#include <fstream>
#include <memory>
#include <utility>

// Delay before the selected row is actually rendered into the result panel.
// Long enough to skip over rows the user scrolls past, short enough not to
// feel sticky when the selection settles.
static constexpr int kShowDebounceMs = 200;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MainWindow::MainWindow()
{
  auto gtk_settings = Gtk::Settings::get_default();
  if (gtk_settings)
  {
    // Force Adwaita theme which contains the appropriate CSS definitions and
    // SVG symbolic icons. Over SSH/bare window managers on Rocky 8, GTK 3.22
    // often falls back to a legacy engine where SpinButton/ComboBox element
    // icons (+/- and down arrows) fail to render.
    gtk_settings->property_gtk_theme_name() = "Adwaita";
    gtk_settings->property_gtk_icon_theme_name() = "Adwaita";
  }

  set_title("SmartMet Server Compare");
  set_default_size(1200, 800);

  // Result viewers, registered most-specific first.  TextDiffViewer is the
  // catch-all and goes last.
  result_panel_.add_viewer(std::make_unique<ImageDiffViewer>());
  result_panel_.add_viewer(std::make_unique<TextDiffViewer>());

  vpaned_.pack1(list_view_, false, true);
  vpaned_.pack2(result_panel_, true, true);
  vpaned_.set_position(220);

  main_box_.pack_start(input_bar_, false, false);
  main_box_.pack_start(sep_, false, false);
  main_box_.pack_start(vpaned_, true, true);
  main_box_.pack_start(status_panel_, false, false);
  add(main_box_);

  // Wire signals
  input_bar_.signal_fetch().connect(sigc::mem_fun(*this, &MainWindow::on_fetch_requested));
  input_bar_.signal_load_file().connect(sigc::mem_fun(*this, &MainWindow::on_load_file_requested));
  input_bar_.signal_save_file().connect(sigc::mem_fun(*this, &MainWindow::on_save_file_requested));
  input_bar_.signal_compare().connect(sigc::mem_fun(*this, &MainWindow::on_compare_requested));
  input_bar_.signal_rerun_filtered().connect(sigc::mem_fun(*this, &MainWindow::on_rerun_filtered_requested));
  input_bar_.signal_stop().connect(sigc::mem_fun(*this, &MainWindow::on_stop_requested));

  list_view_.signal_index_selected().connect(sigc::mem_fun(*this, &MainWindow::on_row_selected));
  list_view_.signal_inspect_requested().connect(sigc::mem_fun(*this, &MainWindow::on_inspect_requested));
  list_view_.signal_query_edited().connect(sigc::mem_fun(*this, &MainWindow::on_query_edited));

  fetch_dispatcher_.connect(sigc::mem_fun(*this, &MainWindow::on_fetch_dispatch));
  show_dispatcher_.connect(sigc::mem_fun(*this, &MainWindow::on_show_dispatch));

  runner_.signal_result().connect(sigc::mem_fun(*this, &MainWindow::on_compare_result));
  runner_.signal_done().connect(sigc::mem_fun(*this, &MainWindow::on_compare_done));

  status_panel_.set_status("Enter source server URL and click \"Fetch queries\".");
  show_all_children();
}

MainWindow::~MainWindow()
{
  // Make sure the show worker is wound down before our members are
  // destroyed — the worker thread holds raw `this` pointers into
  // result_panel_, show_mutex_, etc.
  show_debounce_.disconnect();
  if (show_cancel_)
    show_cancel_->store(true);
  if (show_worker_.joinable())
    show_worker_.join();
}

// ---------------------------------------------------------------------------
// Fetch
// ---------------------------------------------------------------------------

void MainWindow::on_fetch_requested()
{
  const std::string url    = input_bar_.source_url();
  const std::string prefix = input_bar_.prefix();
  const int         minutes = input_bar_.minutes();

  if (url.empty())
  {
    status_panel_.set_status("Enter a source server URL first.");
    return;
  }

  input_bar_.start_fetching();
  list_view_.clear();
  queries_.clear();
  results_.clear();
  result_panel_.clear();

  status_panel_.set_status("Fetching queries from source server…");
  status_panel_.pulse();

  fetcher_.fetch_async(
      url,
      prefix,
      minutes,
      [this](std::vector<QueryInfo> queries, std::string error)
      {
        std::lock_guard<std::mutex> lock(fetch_mutex_);
        fetch_result_ = {std::move(queries), std::move(error)};
        fetch_dispatcher_.emit();
      });
}

void MainWindow::on_fetch_dispatch()
{
  std::pair<std::vector<QueryInfo>, std::string> result;
  {
    std::lock_guard<std::mutex> lock(fetch_mutex_);
    result = std::move(fetch_result_);
  }

  status_panel_.set_progress(0.0);

  if (!result.second.empty())
  {
    input_bar_.set_idle(false);
    status_panel_.set_status("Fetch error: " + result.second);
    return;
  }

  on_queries_fetched(std::move(result.first));
}

void MainWindow::on_save_file_requested()
{
  // Snapshot the visible (filtered) request strings before opening the
  // dialog so the export reflects exactly what the user can see right
  // now, even if the filter or selection changes during the dialog.
  const auto rows = list_view_.visible_request_strings();
  if (rows.empty())
  {
    Gtk::MessageDialog dlg(*this, "No queries match the current filter.",
                           false, Gtk::MESSAGE_INFO);
    dlg.run();
    return;
  }

  Gtk::FileChooserDialog dlg(*this, "Save filtered queries to file",
                             Gtk::FILE_CHOOSER_ACTION_SAVE);
  dlg.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dlg.add_button("_Save",   Gtk::RESPONSE_OK);
  dlg.set_do_overwrite_confirmation(true);
  dlg.set_current_name("queries.txt");

  auto filter_txt = Gtk::FileFilter::create();
  filter_txt->set_name("Request lists (*.txt)");
  filter_txt->add_pattern("*.txt");
  dlg.add_filter(filter_txt);

  auto filter_all = Gtk::FileFilter::create();
  filter_all->set_name("All files");
  filter_all->add_pattern("*");
  dlg.add_filter(filter_all);

  if (dlg.run() != Gtk::RESPONSE_OK)
    return;

  const std::string path = dlg.get_filename();
  std::ofstream f(path);
  if (!f)
  {
    Gtk::MessageDialog err(*this, "Cannot open file for writing: " + path,
                           false, Gtk::MESSAGE_ERROR);
    err.run();
    return;
  }

  f << "# " << rows.size()
    << " request(s) exported by smartmet-server-compare\n";
  for (const auto& r : rows)
    f << r << '\n';

  status_panel_.set_status("Saved " + std::to_string(rows.size()) +
                           " queries to " + path);
}

void MainWindow::on_load_file_requested()
{
  Gtk::FileChooserDialog dlg(*this, "Select request-list file", Gtk::FILE_CHOOSER_ACTION_OPEN);
  dlg.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dlg.add_button("_Open",   Gtk::RESPONSE_OK);

  auto filter_txt = Gtk::FileFilter::create();
  filter_txt->set_name("Request lists and access logs (*.txt, *.log)");
  filter_txt->add_pattern("*.txt");
  filter_txt->add_pattern("*.log");
  dlg.add_filter(filter_txt);

  auto filter_all = Gtk::FileFilter::create();
  filter_all->set_name("All files");
  filter_all->add_pattern("*");
  dlg.add_filter(filter_all);

  if (dlg.run() != Gtk::RESPONSE_OK)
    return;

  const std::string path = dlg.get_filename();
  auto result = QueryFetcher::fetch_from_file(path);

  if (!result.error.empty())
  {
    status_panel_.set_status("File error: " + result.error);
    return;
  }

  // Show warnings about problematic lines if any
  if (!result.problematic_lines.empty())
  {
    std::string msg = "Skipped " + std::to_string(result.problematic_lines.size()) +
                      " unrecognised line(s):\n\n";
    for (size_t i = 0; i < result.problematic_lines.size() && i < 10; ++i)
    {
      const auto& [line_num, reason] = result.problematic_lines[i];
      msg += "Line " + std::to_string(line_num) + ": " + reason + "\n";
    }
    if (result.problematic_lines.size() > 10)
      msg += "\n… and " + std::to_string(result.problematic_lines.size() - 10) + " more.";

    Gtk::MessageDialog warn(*this, msg, false, Gtk::MESSAGE_WARNING);
    warn.run();
  }

  list_view_.clear();
  queries_.clear();
  results_.clear();
  result_panel_.clear();

  on_queries_fetched(std::move(result.queries));
}

void MainWindow::on_queries_fetched(std::vector<QueryInfo> queries)
{
  queries_ = std::move(queries);
  list_view_.populate(queries_);
  results_.assign(queries_.size(), CompareResult{});
  for (int i = 0; i < static_cast<int>(queries_.size()); ++i)
  {
    results_[i].index = i;
    results_[i].request_string = queries_[i].request_string;
  }

  input_bar_.set_idle(!queries_.empty());
  status_panel_.set_status("Fetched " + std::to_string(queries_.size()) +
                           " matching queries.");
}

// ---------------------------------------------------------------------------
// Compare
// ---------------------------------------------------------------------------

void MainWindow::on_compare_requested()
{
  const std::string srv1 = input_bar_.server1_url();
  const std::string srv2 = input_bar_.server2_url();

  if (srv1.empty() || srv2.empty())
  {
    Gtk::MessageDialog dlg(*this, "Please enter both server URLs.", false, Gtk::MESSAGE_WARNING);
    dlg.run();
    return;
  }

  // Reset all results to PENDING
  for (auto& r : results_)
  {
    r.status = CompareStatus::PENDING;
    r.body1.clear();
    r.body2.clear();
    r.formatted1.clear();
    r.formatted2.clear();
    r.error1.clear();
    r.error2.clear();
  }
  list_view_.reset_to_pending();
  result_panel_.clear();

  total_queries_ = static_cast<int>(queries_.size());
  done_queries_  = 0;
  status_panel_.set_progress(0.0);

  input_bar_.start_comparing();
  status_panel_.set_status("Comparing…");

  runner_.start(queries_, srv1, srv2,
                input_bar_.max_concurrent(),
                input_bar_.max_size_mb() * 1024 * 1024);
}

void MainWindow::on_rerun_filtered_requested()
{
  const auto idx = list_view_.visible_indices();
  if (idx.empty())
  {
    Gtk::MessageDialog dlg(*this, "No queries match the current filter.",
                           false, Gtk::MESSAGE_INFO);
    dlg.run();
    return;
  }

  std::vector<QueryInfo> filtered;
  filtered.reserve(idx.size());
  for (int i : idx)
    if (i >= 0 && i < static_cast<int>(queries_.size()))
      filtered.push_back(queries_[i]);

  queries_ = std::move(filtered);
  list_view_.clear();
  results_.clear();
  result_panel_.clear();
  list_view_.populate(queries_);
  results_.assign(queries_.size(), CompareResult{});
  for (int i = 0; i < static_cast<int>(queries_.size()); ++i)
  {
    results_[i].index = i;
    results_[i].request_string = queries_[i].request_string;
  }

  on_compare_requested();
}

void MainWindow::on_stop_requested()
{
  runner_.request_stop();
  input_bar_.notify_stopping();
  status_panel_.set_status("Stopping…");
}

void MainWindow::on_compare_result(CompareResult result)
{
  if (result.status == CompareStatus::RUNNING)
  {
    list_view_.update_status(result);
    return;
  }

  results_[result.index] = result;
  list_view_.update_status(result);

  ++done_queries_;
  if (total_queries_ > 0)
    status_panel_.set_progress(static_cast<double>(done_queries_) / total_queries_);

  if (list_view_.selected_index() == result.index)
    schedule_show(result.index, 0);
}

void MainWindow::on_compare_done()
{
  input_bar_.set_idle(!queries_.empty());
  status_panel_.set_progress(1.0);

  int equal = 0, diff = 0, err = 0;
  for (const auto& r : results_)
  {
    if (r.status == CompareStatus::EQUAL)       ++equal;
    else if (r.status == CompareStatus::DIFFERENT) ++diff;
    else if (r.status == CompareStatus::ERROR)   ++err;
  }

  status_panel_.set_status("Done.  Equal: " + std::to_string(equal) +
                           "  Different: " + std::to_string(diff) +
                           "  Error: " + std::to_string(err));
}

// ---------------------------------------------------------------------------
// Selection & async show pipeline
// ---------------------------------------------------------------------------

void MainWindow::on_row_selected(int index)
{
  // Skip when the user-driven selection hasn't actually changed.  GTK can
  // emit selection-changed during a running compare even though the user
  // never moved the cursor — for instance when ListStore rows update and
  // a TreeModelFilter renumbers visible paths.  Without this guard each
  // such spurious event tears down and rebuilds the active viewer (most
  // visibly: the ImageDiffViewer's animation restarts and any non-default
  // mode the user picked is reset).  The compare-result path bypasses
  // this handler and goes through schedule_show() directly, so a final
  // result for the already-displayed row still re-renders.
  if (index == current_show_idx_)
    return;
  schedule_show(index, kShowDebounceMs);
}

void MainWindow::on_inspect_requested(int index,
                                      RequestListView::InspectTarget target)
{
  if (index < 0 || index >= static_cast<int>(queries_.size()))
    return;

  using RT = RequestListView::InspectTarget;
  using DT = InspectDialog::Target;

  const std::string srv1 = input_bar_.server1_url();
  const std::string srv2 = input_bar_.server2_url();

  // Only require URLs for the servers the user actually wants to hit.
  const bool need1 = (target != RT::Server2);
  const bool need2 = (target != RT::Server1);
  if ((need1 && srv1.empty()) || (need2 && srv2.empty()))
  {
    Gtk::MessageDialog dlg(*this,
        "Please enter the target server URL(s) first.",
        false, Gtk::MESSAGE_WARNING);
    dlg.run();
    return;
  }

  DT dt = DT::Both;
  switch (target)
  {
    case RT::Server1: dt = DT::Server1; break;
    case RT::Server2: dt = DT::Server2; break;
    case RT::Both:    dt = DT::Both;    break;
  }

  InspectDialog dlg(*this, queries_[index].request_string, srv1, srv2, dt);
  dlg.run();
}

void MainWindow::on_query_edited(int index,
                                  const std::string& new_request,
                                  RequestListView::EditAction action)
{
  if (index < 0 || index >= static_cast<int>(queries_.size()))
    return;

  runner_.request_stop();
  cancel_pending_show();
  result_panel_.clear();

  if (action == RequestListView::EditAction::Replace)
  {
    queries_[index].request_string = new_request;
  }
  else  // AddAfter
  {
    QueryInfo q;
    q.request_string = new_request;
    q.time_utc       = queries_[index].time_utc;
    queries_.insert(queries_.begin() + index + 1, std::move(q));
  }

  list_view_.populate(queries_);
  results_.assign(queries_.size(), CompareResult{});
  for (int i = 0; i < static_cast<int>(queries_.size()); ++i)
  {
    results_[i].index          = i;
    results_[i].request_string = queries_[i].request_string;
  }

  input_bar_.set_idle(!queries_.empty());
  status_panel_.set_status("Query list updated (" +
                           std::to_string(queries_.size()) + " queries).");
}

bool MainWindow::on_key_press_event(GdkEventKey* event)
{
  // Ctrl-modifier shortcuts first.  We handle them regardless of which
  // widget has focus so the user can kick off a fetch / compare / load
  // from anywhere in the window.
  if (event->state & GDK_CONTROL_MASK)
  {
    const guint key = gdk_keyval_to_lower(event->keyval);
    switch (key)
    {
      case GDK_KEY_o:
        on_load_file_requested();
        return true;
      case GDK_KEY_f:
        on_fetch_requested();
        return true;
      case GDK_KEY_r:
        on_compare_requested();
        return true;
      case GDK_KEY_q:
        close();
        return true;
    }
  }

  // Non-modifier shortcuts.
  switch (event->keyval)
  {
    case GDK_KEY_F5:
      on_compare_requested();
      return true;
    case GDK_KEY_Escape:
      on_stop_requested();
      return true;
  }

  return Gtk::Window::on_key_press_event(event);
}

void MainWindow::show_result(int index)
{
  // Legacy synchronous path; kept available for callers that want blocking
  // behaviour.  Nothing in MainWindow currently calls it.
  result_panel_.show(results_[index],
                     input_bar_.server1_url(),
                     input_bar_.server2_url());
}

void MainWindow::cancel_pending_show()
{
  // Cancel any debounced call still queued.
  show_debounce_.disconnect();

  // Cancel the in-flight worker, if any.  Flipping the token causes the
  // worker (or the next checkpoint inside it) to bail out; any result it
  // eventually posts is still discarded by on_show_dispatch() via the
  // current_show_idx_ check.  We detach so the thread can finish what it
  // started without holding up the main loop.
  if (show_cancel_)
    show_cancel_->store(true);
  if (show_worker_.joinable())
    show_worker_.detach();
}

void MainWindow::schedule_show(int index, int debounce_ms)
{
  cancel_pending_show();
  current_show_idx_ = index;

  // The clear / pending-state / kick decision is deferred into the timer
  // callback so a transient -1 → N selection blip (e.g. from filter
  // updates while a compare is running) doesn't actually clear the view
  // between debounce ticks.  When debounce_ms is 0 this just runs inline,
  // matching the previous behaviour for compare-result completions.
  auto kick = [this, index]() {
    if (current_show_idx_ != index) return false;

    if (index < 0 || index >= static_cast<int>(results_.size()))
    {
      result_panel_.clear();
      return false;
    }

    const auto& result = results_[index];
    if (result.status == CompareStatus::PENDING ||
        result.status == CompareStatus::RUNNING)
    {
      result_panel_.clear();
      return false;
    }

    kick_show_worker(index);
    return false;  // one-shot
  };

  if (debounce_ms <= 0)
    kick();
  else
    show_debounce_ = Glib::signal_timeout().connect(kick, debounce_ms);
}

void MainWindow::kick_show_worker(int index)
{
  // Retire any previously-finished worker before reassigning the slot;
  // std::thread::operator= onto a joinable thread would std::terminate.
  if (show_worker_.joinable())
    show_worker_.detach();

  show_cancel_ = std::make_shared<std::atomic<bool>>(false);
  auto cancel = show_cancel_;

  // Snapshot the data the worker needs: the result (may be large; copy
  // rather than reference, because the vector can be mutated concurrently
  // on the main thread when new compare results arrive) and the server
  // labels.
  auto result  = results_[index];
  auto server1 = input_bar_.server1_url();
  auto server2 = input_bar_.server2_url();

  show_worker_ = std::thread(
      [this, index, cancel,
       result = std::move(result),
       server1 = std::move(server1),
       server2 = std::move(server2)]() mutable
      {
        auto [viewer, prepared] = result_panel_.prepare_async(result, *cancel);
        if (cancel->load()) return;

        {
          std::lock_guard<std::mutex> lock(show_mutex_);
          show_ready_ = {index, viewer, std::move(prepared),
                         std::move(server1), std::move(server2)};
        }
        show_dispatcher_.emit();
      });
}

void MainWindow::on_show_dispatch()
{
  ShowReady r;
  {
    std::lock_guard<std::mutex> lock(show_mutex_);
    r = std::move(show_ready_);
    show_ready_ = {};
  }

  // Drop stale results: the selection moved on while the worker was busy.
  if (r.idx != current_show_idx_)
    return;

  result_panel_.show_prepared(r.viewer, results_[r.idx],
                              r.server1, r.server2,
                              std::move(r.prepared));

  // Intentionally do NOT touch show_worker_ here: by the time this handler
  // runs a newer schedule_show() may already have replaced the thread slot
  // with a live worker we must not detach.  cancel_pending_show() or the
  // destructor handles retirement.
}
