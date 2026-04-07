#include "MainWindow.h"

#include <gtkmm/cellrenderertext.h>
#include <gtkmm/entry.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/treepath.h>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MainWindow::MainWindow()
{
  set_title("SmartMet Server Compare");
  set_default_size(1200, 800);

  build_ui();

  // Restore history into combo boxes and last-used scalar values
  load_combo(ent_source_, "source_server");
  load_combo(ent_prefix_, "prefix");
  load_combo(ent_srv1_,   "server1");
  load_combo(ent_srv2_,   "server2");
  spin_minutes_.set_value(settings_.get_int("minutes", 2));
  spin_concurrent_.set_value(settings_.get_int("max_concurrent", 4));

  // Wire up dispatcher for fetch thread results
  fetch_dispatcher_.connect(sigc::mem_fun(*this, &MainWindow::on_fetch_dispatch));

  // Wire up compare runner signals
  runner_.signal_result().connect(sigc::mem_fun(*this, &MainWindow::on_compare_result));
  runner_.signal_done().connect(sigc::mem_fun(*this, &MainWindow::on_compare_done));

  btn_stop_.set_sensitive(false);
  btn_compare_.set_sensitive(false);

  set_status("Enter source server URL and click \"Fetch queries\".");
  show_all_children();
}

// ---------------------------------------------------------------------------
// Helpers for ComboBoxText-with-entry
// ---------------------------------------------------------------------------

/* static */
std::string MainWindow::combo_text(const Gtk::ComboBoxText& combo)
{
  return combo.get_active_text();
}

/* static */
void MainWindow::setup_combo_entry(Gtk::ComboBoxText& combo,
                                   const char* placeholder,
                                   int width_chars)
{
  if (auto* e = dynamic_cast<Gtk::Entry*>(combo.get_child()))
  {
    e->set_placeholder_text(placeholder);
    e->set_width_chars(width_chars);
  }
}

void MainWindow::load_combo(Gtk::ComboBoxText& combo, const std::string& key)
{
  const auto items = settings_.history(key);
  combo.remove_all();
  for (const auto& item : items)
    combo.append(item);

  // Pre-fill entry with the most-recently-used value
  if (!items.empty())
    if (auto* e = dynamic_cast<Gtk::Entry*>(combo.get_child()))
      e->set_text(items.front());
}

void MainWindow::save_combo(Gtk::ComboBoxText& combo, const std::string& key)
{
  const std::string val = combo_text(combo);
  if (val.empty())
    return;
  settings_.add_to_history(key, val);
  load_combo(combo, key);  // refresh dropdown order
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void MainWindow::build_ui()
{
  // ---- Row 1: source server ----
  lbl_source_.set_xalign(1.0f);
  setup_combo_entry(ent_source_, "http://host:port", 30);

  lbl_prefix_.set_xalign(1.0f);
  setup_combo_entry(ent_prefix_, "/wfs", 10);

  lbl_minutes_.set_xalign(1.0f);
  spin_minutes_.set_range(1, 1440);
  spin_minutes_.set_increments(1, 10);
  spin_minutes_.set_width_chars(5);

  btn_fetch_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_fetch_clicked));
  btn_load_file_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_load_file_clicked));

  row1_.set_border_width(4);
  row1_.pack_start(lbl_source_, false, false);
  row1_.pack_start(ent_source_, true, true);
  row1_.pack_start(lbl_prefix_, false, false);
  row1_.pack_start(ent_prefix_, false, false);
  row1_.pack_start(lbl_minutes_, false, false);
  row1_.pack_start(spin_minutes_, false, false);
  row1_.pack_start(btn_fetch_, false, false);
  row1_.pack_start(btn_load_file_, false, false);

  // ---- Row 2: target servers ----
  lbl_srv1_.set_xalign(1.0f);
  setup_combo_entry(ent_srv1_, "http://server1:port", 28);

  lbl_srv2_.set_xalign(1.0f);
  setup_combo_entry(ent_srv2_, "http://server2:port", 28);

  lbl_concurrent_.set_xalign(1.0f);
  spin_concurrent_.set_range(1, 32);
  spin_concurrent_.set_increments(1, 4);
  spin_concurrent_.set_width_chars(4);

  btn_compare_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_compare_clicked));
  btn_stop_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_stop_clicked));

  row2_.set_border_width(4);
  row2_.pack_start(lbl_srv1_, false, false);
  row2_.pack_start(ent_srv1_, true, true);
  row2_.pack_start(lbl_srv2_, false, false);
  row2_.pack_start(ent_srv2_, true, true);
  row2_.pack_start(lbl_concurrent_, false, false);
  row2_.pack_start(spin_concurrent_, false, false);
  row2_.pack_start(btn_compare_, false, false);
  row2_.pack_start(btn_stop_, false, false);

  // ---- Request list ----
  setup_request_list();
  list_scroll_.add(list_view_);
  list_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  list_scroll_.set_min_content_height(200);

  // ---- Vertical paned: list + diff ----
  vpaned_.pack1(list_scroll_, false, true);
  vpaned_.pack2(diff_view_, true, true);
  vpaned_.set_position(220);

  // ---- Status bar ----
  progress_.set_show_text(true);
  progress_.set_fraction(0.0);
  progress_.set_size_request(180, -1);
  status_box_.set_border_width(2);
  status_box_.pack_start(progress_, false, false);
  status_box_.pack_start(statusbar_, true, true);

  // ---- Main box ----
  main_box_.pack_start(row1_, false, false);
  main_box_.pack_start(row2_, false, false);
  main_box_.pack_start(sep_, false, false);
  main_box_.pack_start(vpaned_, true, true);
  main_box_.pack_start(status_box_, false, false);

  add(main_box_);
}

void MainWindow::setup_request_list()
{
  list_store_ = Gtk::ListStore::create(columns_);
  list_view_.set_model(list_store_);

  auto* col_status = Gtk::manage(new Gtk::TreeViewColumn("Status"));
  auto* rend_status = Gtk::manage(new Gtk::CellRendererText());
  col_status->pack_start(*rend_status);
  col_status->add_attribute(rend_status->property_text(), columns_.col_status);
  col_status->set_min_width(80);
  list_view_.append_column(*col_status);

  auto* col_time = Gtk::manage(new Gtk::TreeViewColumn("Time (UTC)", columns_.col_time));
  col_time->set_min_width(140);
  list_view_.append_column(*col_time);

  auto* col_req = Gtk::manage(new Gtk::TreeViewColumn("Request", columns_.col_request));
  col_req->set_expand(true);
  list_view_.append_column(*col_req);

  list_view_.get_selection()->signal_changed().connect(
      sigc::mem_fun(*this, &MainWindow::on_selection_changed));
}

// ---------------------------------------------------------------------------
// Fetch
// ---------------------------------------------------------------------------

void MainWindow::on_fetch_clicked()
{
  const std::string url    = combo_text(ent_source_);
  const std::string prefix = combo_text(ent_prefix_);
  const int         minutes = static_cast<int>(spin_minutes_.get_value());

  if (url.empty())
  {
    set_status("Enter a source server URL first.");
    return;
  }

  // Persist before starting (so even a failed fetch remembers the URL)
  save_combo(ent_source_, "source_server");
  save_combo(ent_prefix_, "prefix");
  settings_.set_int("minutes", minutes);

  btn_fetch_.set_sensitive(false);
  btn_compare_.set_sensitive(false);
  list_store_->clear();
  queries_.clear();
  results_.clear();
  diff_view_.clear();

  set_status("Fetching queries from source server…");
  progress_.pulse();

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

  btn_fetch_.set_sensitive(true);
  progress_.set_fraction(0.0);

  if (!result.second.empty())
  {
    set_status("Fetch error: " + result.second);
    return;
  }

  on_queries_fetched(std::move(result.first));
}

void MainWindow::on_load_file_clicked()
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
    set_status("File error: " + error);
    return;
  }

  list_store_->clear();
  queries_.clear();
  results_.clear();
  diff_view_.clear();

  on_queries_fetched(std::move(queries));
}

void MainWindow::on_queries_fetched(std::vector<QueryInfo> queries)
{
  queries_ = std::move(queries);
  populate_list(queries_);
  results_.assign(queries_.size(), CompareResult{});
  for (int i = 0; i < static_cast<int>(queries_.size()); ++i)
  {
    results_[i].index = i;
    results_[i].request_string = queries_[i].request_string;
  }

  btn_compare_.set_sensitive(!queries_.empty());
  set_status("Fetched " + std::to_string(queries_.size()) + " matching queries.");
}

void MainWindow::populate_list(const std::vector<QueryInfo>& queries)
{
  list_store_->clear();
  for (int i = 0; i < static_cast<int>(queries.size()); ++i)
  {
    auto row = *list_store_->append();
    row[columns_.col_index]   = i;
    row[columns_.col_status]  = "PENDING";
    row[columns_.col_time]    = Glib::ustring(queries[i].time_utc);
    row[columns_.col_request] = Glib::ustring(queries[i].request_string);
  }
}

// ---------------------------------------------------------------------------
// Compare
// ---------------------------------------------------------------------------

void MainWindow::on_compare_clicked()
{
  const std::string srv1           = combo_text(ent_srv1_);
  const std::string srv2           = combo_text(ent_srv2_);
  const int         max_concurrent = static_cast<int>(spin_concurrent_.get_value());

  if (srv1.empty() || srv2.empty())
  {
    Gtk::MessageDialog dlg(*this, "Please enter both server URLs.", false, Gtk::MESSAGE_WARNING);
    dlg.run();
    return;
  }

  save_combo(ent_srv1_, "server1");
  save_combo(ent_srv2_, "server2");
  settings_.set_int("max_concurrent", max_concurrent);

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

  for (auto& row : list_store_->children())
    row[columns_.col_status] = "PENDING";

  diff_view_.clear();
  total_queries_ = static_cast<int>(queries_.size());
  done_queries_  = 0;
  progress_.set_fraction(0.0);

  set_buttons_running(true);
  set_status("Comparing…");

  runner_.start(queries_, srv1, srv2, max_concurrent);
}

void MainWindow::on_stop_clicked()
{
  runner_.stop();
  set_buttons_running(false);
  set_status("Stopped.");
}

void MainWindow::on_compare_result(CompareResult result)
{
  if (result.status == CompareStatus::RUNNING)
  {
    update_row(result);
    return;
  }

  results_[result.index] = result;
  update_row(result);

  ++done_queries_;
  if (total_queries_ > 0)
    progress_.set_fraction(static_cast<double>(done_queries_) / total_queries_);

  auto sel = list_view_.get_selection()->get_selected();
  if (sel && (*sel)[columns_.col_index] == result.index)
    show_result_in_diff(result);
}

void MainWindow::on_compare_done()
{
  set_buttons_running(false);
  progress_.set_fraction(1.0);

  int equal = 0, diff = 0, err = 0;
  for (const auto& r : results_)
  {
    if (r.status == CompareStatus::EQUAL)       ++equal;
    else if (r.status == CompareStatus::DIFFERENT) ++diff;
    else if (r.status == CompareStatus::ERROR)   ++err;
  }

  set_status("Done.  Equal: " + std::to_string(equal) +
             "  Different: " + std::to_string(diff) +
             "  Error: " + std::to_string(err));
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void MainWindow::on_selection_changed()
{
  auto sel = list_view_.get_selection()->get_selected();
  if (!sel)
  {
    diff_view_.clear();
    return;
  }

  int idx = (*sel)[columns_.col_index];
  if (idx < 0 || idx >= static_cast<int>(results_.size()))
    return;

  show_result_in_diff(results_[idx]);
}

void MainWindow::show_result_in_diff(const CompareResult& result)
{
  const std::string srv1_url = combo_text(ent_srv1_);
  const std::string srv2_url = combo_text(ent_srv2_);

  if (result.status == CompareStatus::PENDING || result.status == CompareStatus::RUNNING)
  {
    diff_view_.clear();
    return;
  }

  if (result.status == CompareStatus::ERROR)
  {
    diff_view_.set_error(result.error1, result.error2, srv1_url, srv2_url);
    return;
  }

  const bool has_text = !result.formatted1.empty() || !result.formatted2.empty();
  if (has_text)
    diff_view_.set_texts(result.formatted1, result.formatted2, srv1_url, srv2_url);
  else
    diff_view_.set_binary(result.status == CompareStatus::EQUAL, srv1_url, srv2_url);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void MainWindow::update_row(const CompareResult& result)
{
  for (auto& row : list_store_->children())
  {
    if (row[columns_.col_index] == result.index)
    {
      row[columns_.col_status] = status_text(result.status);
      return;
    }
  }
}

Glib::ustring MainWindow::status_text(CompareStatus s)
{
  switch (s)
  {
    case CompareStatus::PENDING:   return "PENDING";
    case CompareStatus::RUNNING:   return "QUERYING";
    case CompareStatus::EQUAL:     return "EQUAL";
    case CompareStatus::DIFFERENT: return "DIFF";
    case CompareStatus::ERROR:     return "ERROR";
  }
  return "?";
}

void MainWindow::set_status(const Glib::ustring& msg)
{
  statusbar_.pop();
  statusbar_.push(msg);
}

void MainWindow::set_buttons_running(bool running)
{
  btn_fetch_.set_sensitive(!running);
  btn_load_file_.set_sensitive(!running);
  btn_compare_.set_sensitive(!running);
  btn_stop_.set_sensitive(running);
}
