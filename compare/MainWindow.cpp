#include "MainWindow.h"

#include "ImageDiffViewer.h"
#include "TextDiffViewer.h"

#include <gtkmm/filechooserdialog.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/settings.h>

#include <memory>

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
  input_bar_.signal_compare().connect(sigc::mem_fun(*this, &MainWindow::on_compare_requested));
  input_bar_.signal_stop().connect(sigc::mem_fun(*this, &MainWindow::on_stop_requested));

  list_view_.signal_index_selected().connect(sigc::mem_fun(*this, &MainWindow::on_row_selected));

  fetch_dispatcher_.connect(sigc::mem_fun(*this, &MainWindow::on_fetch_dispatch));

  runner_.signal_result().connect(sigc::mem_fun(*this, &MainWindow::on_compare_result));
  runner_.signal_done().connect(sigc::mem_fun(*this, &MainWindow::on_compare_done));

  status_panel_.set_status("Enter source server URL and click \"Fetch queries\".");
  show_all_children();
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

void MainWindow::on_load_file_requested()
{
  Gtk::FileChooserDialog dlg(*this, "Select request-list file", Gtk::FILE_CHOOSER_ACTION_OPEN);
  dlg.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dlg.add_button("_Open",   Gtk::RESPONSE_OK);

  auto filter_txt = Gtk::FileFilter::create();
  filter_txt->set_name("Text files (*.txt)");
  filter_txt->add_pattern("*.txt");
  dlg.add_filter(filter_txt);

  auto filter_all = Gtk::FileFilter::create();
  filter_all->set_name("All files");
  filter_all->add_pattern("*");
  dlg.add_filter(filter_all);

  if (dlg.run() != Gtk::RESPONSE_OK)
    return;

  const std::string path = dlg.get_filename();
  auto [queries, error] = QueryFetcher::fetch_from_file(path);

  if (!error.empty())
  {
    status_panel_.set_status("File error: " + error);
    return;
  }

  list_view_.clear();
  queries_.clear();
  results_.clear();
  result_panel_.clear();

  on_queries_fetched(std::move(queries));
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
    show_result(result.index);
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
// Selection
// ---------------------------------------------------------------------------

void MainWindow::on_row_selected(int index)
{
  if (index < 0 || index >= static_cast<int>(results_.size()))
  {
    result_panel_.clear();
    return;
  }
  show_result(index);
}

void MainWindow::show_result(int index)
{
  result_panel_.show(results_[index],
                     input_bar_.server1_url(),
                     input_bar_.server2_url());
}
