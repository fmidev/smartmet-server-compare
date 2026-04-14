#include "RequestListView.h"

#include <gtkmm/cellrenderertext.h>

#include <cmath>
#include <iomanip>
#include <sstream>

RequestListView::RequestListView()
{
  store_ = Gtk::ListStore::create(columns_);
  view_.set_model(store_);

  auto* col_num = Gtk::manage(new Gtk::TreeViewColumn("#", columns_.col_number));
  col_num->set_min_width(40);
  view_.append_column(*col_num);

  auto* col_status = Gtk::manage(new Gtk::TreeViewColumn("Status"));
  auto* rend_status = Gtk::manage(new Gtk::CellRendererText());
  col_status->pack_start(*rend_status);
  col_status->add_attribute(rend_status->property_text(), columns_.col_status);
  col_status->set_min_width(80);
  view_.append_column(*col_status);

  auto* col_psnr = Gtk::manage(new Gtk::TreeViewColumn("PSNR (dB)", columns_.col_psnr));
  col_psnr->set_min_width(80);
  view_.append_column(*col_psnr);

  auto* col_time = Gtk::manage(new Gtk::TreeViewColumn("Time (UTC)", columns_.col_time));
  col_time->set_min_width(140);
  view_.append_column(*col_time);

  auto* col_req = Gtk::manage(new Gtk::TreeViewColumn("Request", columns_.col_request));
  col_req->set_expand(true);
  view_.append_column(*col_req);

  view_.get_selection()->signal_changed().connect(
      sigc::mem_fun(*this, &RequestListView::on_selection_changed_internal));

  add(view_);
  set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  set_min_content_height(200);
}

void RequestListView::populate(const std::vector<QueryInfo>& queries)
{
  store_->clear();
  for (int i = 0; i < static_cast<int>(queries.size()); ++i)
  {
    auto row = *store_->append();
    row[columns_.col_number]  = i + 1;
    row[columns_.col_index]   = i;
    row[columns_.col_status]  = "PENDING";
    row[columns_.col_time]    = Glib::ustring(queries[i].time_utc);
    row[columns_.col_request] = Glib::ustring(queries[i].request_string);
  }
}

void RequestListView::reset_to_pending()
{
  for (auto& row : store_->children())
  {
    row[columns_.col_status] = "PENDING";
    row[columns_.col_psnr]   = "";
  }
}

static Glib::ustring format_psnr(double psnr)
{
  if (std::isnan(psnr))
    return "";
  if (std::isinf(psnr))
    return "∞";
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1) << psnr;
  return oss.str();
}

void RequestListView::update_status(const CompareResult& result)
{
  for (auto& row : store_->children())
  {
    if (row[columns_.col_index] == result.index)
    {
      row[columns_.col_status] = status_text(result.status);
      row[columns_.col_psnr]   = format_psnr(result.psnr);
      return;
    }
  }
}

void RequestListView::clear()
{
  store_->clear();
}

int RequestListView::selected_index()
{
  auto sel = view_.get_selection()->get_selected();
  if (!sel)
    return -1;
  return (*sel)[columns_.col_index];
}

void RequestListView::on_selection_changed_internal()
{
  sig_selected_.emit(selected_index());
}

/* static */
Glib::ustring RequestListView::status_text(CompareStatus s)
{
  switch (s)
  {
    case CompareStatus::PENDING:   return "PENDING";
    case CompareStatus::RUNNING:   return "QUERYING";
    case CompareStatus::EQUAL:     return "EQUAL";
    case CompareStatus::DIFFERENT: return "DIFF";
    case CompareStatus::ERROR:     return "ERROR";
    case CompareStatus::TOO_LARGE: return "TOO_LARGE";
  }
  return "?";
}
