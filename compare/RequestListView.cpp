#include "RequestListView.h"
#include "UrlUtils.h"

#include <gtkmm/cellrenderertext.h>
#include <gtkmm/clipboard.h>
#include <gtkmm/menuitem.h>

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

RequestListView::RequestListView()
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0)
{
  // ---- Filter bar ----
  cb_content_.append("All");
  cb_content_.append("Text");
  cb_content_.append("Image");
  cb_content_.set_active(0);

  cb_status_.append("All");
  cb_status_.append("Equal");
  cb_status_.append("Different");
  cb_status_.append("Error");
  cb_status_.set_active(0);

  spin_psnr_.set_range(0, 999);
  spin_psnr_.set_increments(1, 10);
  spin_psnr_.set_value(0);
  spin_psnr_.set_width_chars(5);
  spin_psnr_.set_sensitive(false);  // only active when Content = Image

  cb_content_.signal_changed().connect(
      sigc::mem_fun(*this, &RequestListView::on_filter_changed));
  cb_status_.signal_changed().connect(
      sigc::mem_fun(*this, &RequestListView::on_filter_changed));
  spin_psnr_.signal_value_changed().connect(
      sigc::mem_fun(*this, &RequestListView::on_filter_changed));

  filter_bar_.set_border_width(4);
  filter_bar_.pack_start(lbl_content_, false, false);
  filter_bar_.pack_start(cb_content_, false, false);
  filter_bar_.pack_start(lbl_status_, false, false, 8);
  filter_bar_.pack_start(cb_status_, false, false);
  filter_bar_.pack_start(lbl_psnr_, false, false, 8);
  filter_bar_.pack_start(spin_psnr_, false, false);

  // ---- Model ----
  store_ = Gtk::ListStore::create(columns_);
  filter_ = Gtk::TreeModelFilter::create(store_);
  filter_->set_visible_func(
      sigc::mem_fun(*this, &RequestListView::filter_func));
  view_.set_model(filter_);

  // ---- Visible columns ----
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

  auto* col_size = Gtk::manage(new Gtk::TreeViewColumn("Size (1 / 2)", columns_.col_size));
  col_size->set_min_width(120);
  view_.append_column(*col_size);

  auto* col_time = Gtk::manage(new Gtk::TreeViewColumn("Time (UTC)", columns_.col_time));
  col_time->set_min_width(140);
  view_.append_column(*col_time);

  auto* col_req = Gtk::manage(new Gtk::TreeViewColumn("Request", columns_.col_request));
  col_req->set_expand(true);
  view_.append_column(*col_req);

  view_.get_selection()->signal_changed().connect(
      sigc::mem_fun(*this, &RequestListView::on_selection_changed_internal));

  // ---- Context menu ----
  auto* item_copy_dec = Gtk::manage(new Gtk::MenuItem("Copy request (decoded)"));
  item_copy_dec->signal_activate().connect(
      sigc::mem_fun(*this, &RequestListView::on_copy_decoded));
  context_menu_.append(*item_copy_dec);

  auto* item_copy_enc = Gtk::manage(new Gtk::MenuItem("Copy request (original)"));
  item_copy_enc->signal_activate().connect(
      sigc::mem_fun(*this, &RequestListView::on_copy_encoded));
  context_menu_.append(*item_copy_enc);

  context_menu_.show_all();

  view_.signal_button_press_event().connect(
      sigc::mem_fun(*this, &RequestListView::on_button_press), false);

  // ---- Layout ----
  scroll_.add(view_);
  scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  scroll_.set_overlay_scrolling(false);

  pack_start(filter_bar_, false, false);
  pack_start(scroll_, true, true);
}

// ---------------------------------------------------------------------------
// List operations
// ---------------------------------------------------------------------------

void RequestListView::populate(const std::vector<QueryInfo>& queries)
{
  store_->clear();
  for (int i = 0; i < static_cast<int>(queries.size()); ++i)
  {
    auto row = *store_->append();
    row[columns_.col_number]     = i + 1;
    row[columns_.col_index]      = i;
    row[columns_.col_status]     = "PENDING";
    row[columns_.col_size]       = "";
    row[columns_.col_time]       = Glib::ustring(queries[i].time_utc);
    row[columns_.col_request]     = Glib::ustring(
        urldecode(queries[i].request_string));
    row[columns_.col_request_raw] = Glib::ustring(queries[i].request_string);
    row[columns_.col_raw_status]  = static_cast<int>(CompareStatus::PENDING);
    row[columns_.col_is_image]   = false;
    row[columns_.col_psnr_val]   = std::numeric_limits<double>::quiet_NaN();
  }
}

void RequestListView::reset_to_pending()
{
  for (auto& row : store_->children())
  {
    row[columns_.col_status]     = "PENDING";
    row[columns_.col_psnr]       = "";
    row[columns_.col_size]       = "";
    row[columns_.col_raw_status] = static_cast<int>(CompareStatus::PENDING);
    row[columns_.col_is_image]   = false;
    row[columns_.col_psnr_val]   = std::numeric_limits<double>::quiet_NaN();
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

static std::string format_bytes(std::size_t bytes)
{
  constexpr double KB = 1024.0;
  constexpr double MB = 1024.0 * 1024.0;
  constexpr double GB = 1024.0 * 1024.0 * 1024.0;

  std::ostringstream oss;
  if (bytes < 1024)
    oss << bytes << " B";
  else if (bytes < 1024 * 1024)
    oss << std::fixed << std::setprecision(1) << (bytes / KB) << " KB";
  else if (bytes < static_cast<std::size_t>(1024) * 1024 * 1024)
    oss << std::fixed << std::setprecision(1) << (bytes / MB) << " MB";
  else
    oss << std::fixed << std::setprecision(2) << (bytes / GB) << " GB";
  return oss.str();
}

static Glib::ustring format_size_pair(std::size_t s1, std::size_t s2)
{
  if (s1 == 0 && s2 == 0)
    return "";
  const auto a = format_bytes(s1);
  const auto b = format_bytes(s2);
  if (a == b)
    return Glib::ustring(a);
  return Glib::ustring(a + " / " + b);
}

void RequestListView::update_status(const CompareResult& result)
{
  for (auto& row : store_->children())
  {
    if (row[columns_.col_index] == result.index)
    {
      row[columns_.col_status]     = status_text(result.status);
      row[columns_.col_psnr]       = format_psnr(result.psnr);
      row[columns_.col_size]       = format_size_pair(result.body1.size(),
                                                       result.body2.size());
      row[columns_.col_raw_status] = static_cast<int>(result.status);
      row[columns_.col_is_image]   = is_image_kind(result.kind1) ||
                                     is_image_kind(result.kind2);
      row[columns_.col_psnr_val]   = result.psnr;
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

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------

void RequestListView::on_filter_changed()
{
  // Enable PSNR spinner only when content filter is "Image"
  spin_psnr_.set_sensitive(cb_content_.get_active_row_number() == 2);
  filter_->refilter();
}

bool RequestListView::filter_func(const Gtk::TreeModel::const_iterator& iter)
{
  const auto raw_status = static_cast<CompareStatus>(
      static_cast<int>((*iter)[columns_.col_raw_status]));

  // Always show rows that haven't completed yet
  if (raw_status == CompareStatus::PENDING ||
      raw_status == CompareStatus::RUNNING)
    return true;

  // ---- Content type filter ----
  const int content_idx = cb_content_.get_active_row_number();
  const bool is_image = (*iter)[columns_.col_is_image];

  if (content_idx == 1 && is_image)    // "Text" selected but row is image
    return false;
  if (content_idx == 2 && !is_image)   // "Image" selected but row is text
    return false;

  // ---- PSNR threshold (only when content filter is "Image") ----
  if (content_idx == 2)
  {
    const double threshold = spin_psnr_.get_value();
    if (threshold > 0)
    {
      const double psnr = (*iter)[columns_.col_psnr_val];
      // Hide if PSNR is above the threshold (too similar) or not computed
      if (std::isnan(psnr) || std::isinf(psnr) || psnr > threshold)
        return false;
    }
  }

  // ---- Status filter ----
  const int status_idx = cb_status_.get_active_row_number();

  switch (status_idx)
  {
    case 0:  // All
      return true;
    case 1:  // Equal
      return raw_status == CompareStatus::EQUAL;
    case 2:  // Different
      return raw_status == CompareStatus::DIFFERENT;
    case 3:  // Error
      return raw_status == CompareStatus::ERROR ||
             raw_status == CompareStatus::TOO_LARGE;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Selection & context menu
// ---------------------------------------------------------------------------

void RequestListView::on_selection_changed_internal()
{
  sig_selected_.emit(selected_index());
}

bool RequestListView::on_button_press(GdkEventButton* event)
{
  if (event->type == GDK_BUTTON_PRESS && event->button == 3)
  {
    Gtk::TreeModel::Path path;
    if (view_.get_path_at_pos(static_cast<int>(event->x),
                              static_cast<int>(event->y),
                              path))
    {
      view_.get_selection()->select(path);
    }
    context_menu_.popup_at_pointer(reinterpret_cast<GdkEvent*>(event));
    return true;
  }
  return false;
}

void RequestListView::on_copy_decoded()
{
  auto sel = view_.get_selection()->get_selected();
  if (!sel)
    return;
  Glib::ustring text = (*sel)[columns_.col_request];
  Gtk::Clipboard::get()->set_text(text);
}

void RequestListView::on_copy_encoded()
{
  auto sel = view_.get_selection()->get_selected();
  if (!sel)
    return;
  Glib::ustring text = (*sel)[columns_.col_request_raw];
  Gtk::Clipboard::get()->set_text(text);
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

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
