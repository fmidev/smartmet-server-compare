#include "DiffView.h"

#include <dtl/dtl.hpp>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Character-level SES for two strings.
using CharSes = std::vector<std::pair<char, dtl::elemInfo>>;

// Precomputed diff data, produced off the main thread and applied on it.
struct DiffView::PreparedDiff
{
  // Line-level SES from dtl::Diff::compose().
  std::vector<std::pair<std::string, dtl::elemInfo>> ses;
  // For each non-common block, a character-level SES for the first
  // min(dels, adds) paired del/add lines.  Stored flat in block order; the
  // consumer walks ses and pulls the next `pairs` entries from here.
  std::vector<CharSes> paired_cses;
};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::vector<std::string> split_lines(const std::string& text)
{
  std::vector<std::string> lines;
  std::istringstream ss(text);
  std::string line;
  while (std::getline(ss, line))
    lines.push_back(line);
  return lines;
}

static CharSes char_ses(const std::string& from, const std::string& to)
{
  dtl::Diff<char, std::string> d(from, to);
  d.compose();
  return d.getSes().getSequence();
}

// Insert one line of text at `mark`, applying a whole-line tag and optionally
// a character-level tag on top.
//
//   buf          – target buffer
//   mark         – right-gravity mark at the append position
//   text         – line text (newline is appended automatically)
//   line_tag     – applied to the entire inserted line
//   char_tag     – applied only to differing character runs (may be null)
//   ses          – character-level SES for the paired line (may be null)
//   keep_type    – SES_DELETE for the left/delete pane, SES_ADD for the right/insert pane
//                  characters with this type (or SES_COMMON) are part of our text;
//                  characters of the opposite type belong to the other pane only
static void insert_line(Glib::RefPtr<Gtk::TextBuffer>&          buf,
                        Glib::RefPtr<Gtk::TextMark>&             mark,
                        const std::string&                       text,
                        const Glib::RefPtr<Gtk::TextBuffer::Tag>& line_tag,
                        const Glib::RefPtr<Gtk::TextBuffer::Tag>& char_tag,
                        const CharSes*                            ses,
                        dtl::edit_t                              keep_type)
{
  const int start = mark->get_iter().get_offset();
  buf->insert(mark->get_iter(), text + "\n");
  buf->apply_tag(line_tag, buf->get_iter_at_offset(start), mark->get_iter());

  if (!ses || !char_tag)
    return;

  // Walk the character SES.  For each character that belongs to our pane
  // (keep_type or COMMON), advance `pos`.  Highlight runs where type ==
  // keep_type (i.e. the char is absent from the other pane → it changed).
  int pos = start;
  int run_start = -1;  // start of the current highlighted run (-1 = no run)

  auto flush_run = [&](int end_pos)
  {
    if (run_start >= 0 && end_pos > run_start)
    {
      buf->apply_tag(char_tag,
                     buf->get_iter_at_offset(run_start),
                     buf->get_iter_at_offset(end_pos));
      run_start = -1;
    }
  };

  for (const auto& [ch, info] : *ses)
  {
    const dtl::edit_t t = info.type;

    if (t == dtl::SES_COMMON)
    {
      flush_run(pos);
      ++pos;
    }
    else if (t == keep_type)
    {
      // Differing character on our side – start/continue highlighted run
      if (run_start < 0)
        run_start = pos;
      ++pos;
    }
    // else: t == the other side's type; character not in our text, skip
  }
  flush_run(pos);
}

// Insert an empty placeholder line at mark.
static void insert_placeholder(Glib::RefPtr<Gtk::TextBuffer>&          buf,
                                Glib::RefPtr<Gtk::TextMark>&             mark,
                                const Glib::RefPtr<Gtk::TextBuffer::Tag>& tag)
{
  const int start = mark->get_iter().get_offset();
  buf->insert(mark->get_iter(), "\n");
  buf->apply_tag(tag, buf->get_iter_at_offset(start), mark->get_iter());
}

// ---------------------------------------------------------------------------
// DiffView – construction
// ---------------------------------------------------------------------------

DiffView::DiffView() : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0)
{
  init_column(left_col_, left_label_, left_scroll_, left_view_);
  init_column(right_col_, right_label_, right_scroll_, right_view_);

  // Nav row: prev/next diff buttons + info label.
  diff_info_label_.set_xalign(0.0f);
  diff_info_label_.set_margin_start(8);
  nav_row_.set_margin_start(4);
  nav_row_.set_margin_end(4);
  nav_row_.set_margin_top(2);
  nav_row_.set_margin_bottom(2);
  nav_row_.pack_start(btn_prev_diff_, false, false, 0);
  nav_row_.pack_start(btn_next_diff_, false, false, 0);
  nav_row_.pack_start(diff_info_label_, true, true, 0);
  btn_prev_diff_.signal_clicked().connect(sigc::mem_fun(*this, &DiffView::on_prev_diff));
  btn_next_diff_.signal_clicked().connect(sigc::mem_fun(*this, &DiffView::on_next_diff));

  // Minimap beside the right scrollbar.
  minimap_.set_size_request(14, -1);
  minimap_.add_events(Gdk::BUTTON_PRESS_MASK);
  minimap_.signal_draw().connect(sigc::mem_fun(*this, &DiffView::on_minimap_draw));
  minimap_.signal_button_press_event().connect(
      sigc::mem_fun(*this, &DiffView::on_minimap_button_press));

  // Tab / Shift-Tab / F3 / Shift-F3 jump between differences when either
  // pane has keyboard focus.  `false` inserts the handler BEFORE the
  // default TextView handler so Tab doesn't fall through to focus
  // traversal or default bindings.
  left_view_.signal_key_press_event().connect(
      sigc::mem_fun(*this, &DiffView::on_textview_key_press), false);
  right_view_.signal_key_press_event().connect(
      sigc::mem_fun(*this, &DiffView::on_textview_key_press), false);

  h_box_.pack_start(left_col_, true, true, 0);
  h_box_.pack_start(vseparator_, false, false, 0);
  h_box_.pack_start(right_col_, true, true, 0);
  h_box_.pack_start(minimap_, false, false, 0);

  // Use a common horizontal scrollbar for both text views
  left_scroll_.set_policy(Gtk::POLICY_EXTERNAL, Gtk::POLICY_AUTOMATIC);
  right_scroll_.set_policy(Gtk::POLICY_EXTERNAL, Gtk::POLICY_AUTOMATIC);

  shared_hadj_ = Gtk::Adjustment::create(0.0, 0.0, 0.0, 0.1, 0.1, 0.0);
  hscrollbar_.set_orientation(Gtk::ORIENTATION_HORIZONTAL);
  hscrollbar_.set_adjustment(shared_hadj_);

  // Search bar – packed between the nav row and the text area.
  {
    auto* close_btn = Gtk::manage(new Gtk::Button("✕"));
    close_btn->set_relief(Gtk::RELIEF_NONE);
    close_btn->signal_clicked().connect(sigc::mem_fun(*this, &DiffView::close_search));
    auto* find_lbl = Gtk::manage(new Gtk::Label("Find:"));
    search_entry_.set_placeholder_text("Search…");
    search_entry_.set_size_request(220, -1);
    search_entry_.signal_search_changed().connect(
        sigc::mem_fun(*this, &DiffView::run_search));
    search_entry_.signal_key_press_event().connect(
        sigc::mem_fun(*this, &DiffView::on_search_key_press), false);
    btn_search_prev_.signal_clicked().connect([this] { search_jump(-1); });
    btn_search_next_.signal_clicked().connect([this] { search_jump(+1); });
    search_info_label_.set_width_chars(12);
    search_info_label_.set_xalign(0.0f);
    search_bar_.set_margin_top(2);
    search_bar_.set_margin_bottom(2);
    search_bar_.set_margin_start(4);
    search_bar_.pack_start(*close_btn,        false, false, 0);
    search_bar_.pack_start(*find_lbl,         false, false, 0);
    search_bar_.pack_start(search_entry_,     false, false, 0);
    search_bar_.pack_start(btn_search_prev_,  false, false, 0);
    search_bar_.pack_start(btn_search_next_,  false, false, 0);
    search_bar_.pack_start(search_info_label_,false, false, 0);
  }

  pack_start(nav_row_,    false, false, 0);
  pack_start(search_bar_, false, false, 0);
  pack_start(h_box_,      true,  true,  0);
  pack_start(hscrollbar_, false, false, 0);

  update_diff_info_label();

  auto lbuf = left_view_.get_buffer();
  auto rbuf = right_view_.get_buffer();

  // Line-level tags – created first so they get lower priority in the tag
  // table than the character-level tags defined below.
  tag_del_ = lbuf->create_tag("del");
  tag_del_->property_background() = "#ffcccc";  // light red

  tag_ins_ = rbuf->create_tag("ins");
  tag_ins_->property_background() = "#ccffcc";  // light green

  tag_ph_left_ = lbuf->create_tag("ph");
  tag_ph_left_->property_background() = "#e8e8e8";
  tag_ph_left_->property_foreground() = "#b0b0b0";

  tag_ph_right_ = rbuf->create_tag("ph");
  tag_ph_right_->property_background() = "#e8e8e8";
  tag_ph_right_->property_foreground() = "#b0b0b0";

  // Character-level tags – created after line-level tags so they have higher
  // priority and override the line background on individual characters.
  tag_del_char_ = lbuf->create_tag("del_c");
  tag_del_char_->property_background() = "#ff8888";  // darker red

  tag_ins_char_ = rbuf->create_tag("ins_c");
  tag_ins_char_->property_background() = "#88ff88";  // darker green

  // Search highlight tags – created after diff tags so they take priority.
  tag_search_left_ = lbuf->create_tag("search");
  tag_search_left_->property_background() = "#ffff66";
  tag_search_right_ = rbuf->create_tag("search");
  tag_search_right_->property_background() = "#ffff66";
  tag_search_cur_left_ = lbuf->create_tag("search_cur");
  tag_search_cur_left_->property_background() = "#ffaa00";
  tag_search_cur_right_ = rbuf->create_tag("search_cur");
  tag_search_cur_right_->property_background() = "#ffaa00";

  connect_scroll_sync();
  show_all();
  search_bar_.hide();  // shown only when the user opens search with Ctrl+F
}

void DiffView::init_column(Gtk::Box& col,
                           Gtk::Label& lbl,
                           Gtk::ScrolledWindow& scroll,
                           Gtk::TextView& view)
{
  lbl.set_xalign(0.0f);
  lbl.set_margin_start(4);
  lbl.set_margin_top(2);
  lbl.set_margin_bottom(2);
  lbl.override_color(Gdk::RGBA("white"));

  Gdk::RGBA header_bg;
  header_bg.set_rgba(0.2, 0.2, 0.2, 1.0);
  lbl.override_background_color(header_bg);

  view.set_editable(false);
  view.set_cursor_visible(false);
  view.set_wrap_mode(Gtk::WRAP_NONE);
  view.set_monospace(true);

  scroll.add(view);
  // H-policy is configured later to use the external scrollbar
  scroll.set_policy(Gtk::POLICY_EXTERNAL, Gtk::POLICY_AUTOMATIC);

  col.pack_start(lbl, false, false, 0);
  col.pack_start(scroll, true, true, 0);
}

void DiffView::connect_scroll_sync()
{
  auto lva = left_scroll_.get_vadjustment();
  auto rva = right_scroll_.get_vadjustment();

  lva->signal_value_changed().connect(
      [this, lva, rva]()
      {
        if (syncing_scroll_) return;
        syncing_scroll_ = true;
        rva->set_value(lva->get_value());
        syncing_scroll_ = false;
        minimap_.queue_draw();
      });

  rva->signal_value_changed().connect(
      [this, lva, rva]()
      {
        if (syncing_scroll_) return;
        syncing_scroll_ = true;
        lva->set_value(rva->get_value());
        syncing_scroll_ = false;
        minimap_.queue_draw();
      });

  lva->signal_changed().connect([this]() { minimap_.queue_draw(); });

  auto lha = left_scroll_.get_hadjustment();
  auto rha = right_scroll_.get_hadjustment();

  auto update_shared_hadj_bounds = [this, lha, rha]()
  {
    double lower = std::min(lha->get_lower(), rha->get_lower());
    double upper = std::max(lha->get_upper(), rha->get_upper());
    double page_size = std::max(lha->get_page_size(), rha->get_page_size());
    double step_inc = std::max(lha->get_step_increment(), rha->get_step_increment());
    double page_inc = std::max(lha->get_page_increment(), rha->get_page_increment());
    double val = shared_hadj_->get_value();
    
    // Clamp value to the new upper bound
    if (val > upper - page_size)
      val = std::max(lower, upper - page_size);

    shared_hadj_->configure(val, lower, upper, step_inc, page_inc, page_size);
  };

  lha->signal_changed().connect(update_shared_hadj_bounds);
  rha->signal_changed().connect(update_shared_hadj_bounds);

  // When either view is scrolled (e.g. via keyboard), update the shared scrollbar
  lha->signal_value_changed().connect([this, lha]() {
    if (!syncing_scroll_) shared_hadj_->set_value(lha->get_value());
  });
  rha->signal_value_changed().connect([this, rha]() {
    if (!syncing_scroll_) shared_hadj_->set_value(rha->get_value());
  });

  // When the shared scrollbar is moved, update both views
  shared_hadj_->signal_value_changed().connect([this, lha, rha]() {
    if (syncing_scroll_) return;
    syncing_scroll_ = true;
    double val = shared_hadj_->get_value();
    lha->set_value(val);
    rha->set_value(val);
    syncing_scroll_ = false;
  });
}

void DiffView::set_labels(const std::string& label1, const std::string& label2)
{
  left_label_.set_text("  " + label1);
  right_label_.set_text("  " + label2);
}

// ---------------------------------------------------------------------------
// Text diff using dtl – two-level (line + character)
// ---------------------------------------------------------------------------

std::shared_ptr<DiffView::PreparedDiff>
DiffView::compute_diff(const std::string& text1,
                        const std::string& text2,
                        const std::atomic<bool>& cancel_token)
{
  auto prepared = std::make_shared<PreparedDiff>();

  const auto lines1 = split_lines(text1);
  const auto lines2 = split_lines(text2);

  dtl::Diff<std::string, std::vector<std::string>> d(lines1, lines2);
  d.compose();
  if (cancel_token.load()) return {};
  prepared->ses = d.getSes().getSequence();

  // Precompute the character-level SES for every paired DELETE/ADD line.
  // `paired_cses` is stored flat in block order so apply_prepared() can walk
  // the line SES and pull `pairs` consecutive entries per non-common block.
  const auto& ses = prepared->ses;
  std::size_t i = 0;
  while (i < ses.size())
  {
    if (ses[i].second.type == dtl::SES_COMMON) { ++i; continue; }

    std::vector<std::string> dels, adds;
    while (i < ses.size() && ses[i].second.type != dtl::SES_COMMON)
    {
      if (ses[i].second.type == dtl::SES_DELETE)
        dels.push_back(ses[i].first);
      else
        adds.push_back(ses[i].first);
      ++i;
    }
    const std::size_t pairs = std::min(dels.size(), adds.size());
    for (std::size_t j = 0; j < pairs; ++j)
    {
      if (cancel_token.load()) return {};
      prepared->paired_cses.push_back(char_ses(dels[j], adds[j]));
    }
  }

  return prepared;
}

void DiffView::apply_prepared(const std::shared_ptr<PreparedDiff>& prepared,
                               const std::string& label1,
                               const std::string& label2)
{
  set_labels(label1, label2);
  reset_diff_navigation();

  auto lbuf = left_view_.get_buffer();
  auto rbuf = right_view_.get_buffer();
  lbuf->set_text("");
  rbuf->set_text("");

  if (!prepared)
  {
    update_diff_info_label();
    minimap_.queue_draw();
    return;
  }

  auto lmark = lbuf->create_mark(lbuf->end(), false);
  auto rmark = rbuf->create_mark(rbuf->end(), false);

  const auto& ses = prepared->ses;
  const auto& paired = prepared->paired_cses;
  std::size_t pair_idx = 0;
  std::size_t i = 0;
  int cur_line = 0;

  while (i < ses.size())
  {
    const auto& [elem, info] = ses[i];

    if (info.type == dtl::SES_COMMON)
    {
      lbuf->insert(lmark->get_iter(), elem + "\n");
      rbuf->insert(rmark->get_iter(), elem + "\n");
      ++i;
      ++cur_line;
      continue;
    }

    std::vector<std::string> dels, adds;
    while (i < ses.size() && ses[i].second.type != dtl::SES_COMMON)
    {
      if (ses[i].second.type == dtl::SES_DELETE)
        dels.push_back(ses[i].first);
      else
        adds.push_back(ses[i].first);
      ++i;
    }

    const int block_start = cur_line;
    const std::size_t pairs = std::min(dels.size(), adds.size());

    for (std::size_t j = 0; j < pairs; ++j)
    {
      const auto& cses = paired[pair_idx++];
      insert_line(lbuf, lmark, dels[j], tag_del_, tag_del_char_, &cses, dtl::SES_DELETE);
      insert_line(rbuf, rmark, adds[j], tag_ins_, tag_ins_char_, &cses, dtl::SES_ADD);
      ++cur_line;
    }

    for (std::size_t j = pairs; j < dels.size(); ++j)
    {
      insert_line(lbuf, lmark, dels[j], tag_del_, {}, nullptr, dtl::SES_DELETE);
      insert_placeholder(rbuf, rmark, tag_ph_right_);
      ++cur_line;
    }

    for (std::size_t j = pairs; j < adds.size(); ++j)
    {
      insert_placeholder(lbuf, lmark, tag_ph_left_);
      insert_line(rbuf, rmark, adds[j], tag_ins_, {}, nullptr, dtl::SES_ADD);
      ++cur_line;
    }

    diff_ranges_.emplace_back(block_start, cur_line - 1);
  }

  lbuf->delete_mark(lmark);
  rbuf->delete_mark(rmark);

  total_lines_ = cur_line;
  update_diff_info_label();
  minimap_.queue_draw();

  // Centre on the first difference so the user doesn't have to hunt for it
  // when the two responses differ in only a few lines out of thousands.
  if (!diff_ranges_.empty())
  {
    current_diff_ = 0;
    scroll_to_line(diff_ranges_.front().first);
    update_diff_info_label();
  }
}

void DiffView::set_texts(const std::string& text1,
                         const std::string& text2,
                         const std::string& label1,
                         const std::string& label2)
{
  std::atomic<bool> no_cancel{false};
  apply_prepared(compute_diff(text1, text2, no_cancel), label1, label2);
}

// ---------------------------------------------------------------------------
// Non-text / error helpers
// ---------------------------------------------------------------------------

void DiffView::set_binary(bool equal, const std::string& label1, const std::string& label2)
{
  set_labels(label1, label2);
  reset_diff_navigation();
  const char* msg = equal ? "[binary content – identical]" : "[binary content – DIFFERENT]";
  left_view_.get_buffer()->set_text(msg);
  right_view_.get_buffer()->set_text(msg);
  update_diff_info_label();
  minimap_.queue_draw();
}

void DiffView::set_error(const std::string& msg1,
                         const std::string& msg2,
                         const std::string& label1,
                         const std::string& label2)
{
  set_labels(label1, label2);
  reset_diff_navigation();
  left_view_.get_buffer()->set_text(msg1.empty() ? "(no error)" : msg1);
  right_view_.get_buffer()->set_text(msg2.empty() ? "(no error)" : msg2);
  update_diff_info_label();
  minimap_.queue_draw();
}

void DiffView::clear()
{
  left_label_.set_text("  Server 1");
  right_label_.set_text("  Server 2");
  left_view_.get_buffer()->set_text("");
  right_view_.get_buffer()->set_text("");
  reset_diff_navigation();
  update_diff_info_label();
  minimap_.queue_draw();
}

// ---------------------------------------------------------------------------
// Diff navigation
// ---------------------------------------------------------------------------

void DiffView::reset_diff_navigation()
{
  diff_ranges_.clear();
  total_lines_ = 0;
  current_diff_ = -1;
}

void DiffView::update_diff_info_label()
{
  const bool has_diffs = !diff_ranges_.empty();
  btn_prev_diff_.set_sensitive(has_diffs);
  btn_next_diff_.set_sensitive(has_diffs);

  if (!has_diffs)
    diff_info_label_.set_text("No differences");
  else if (current_diff_ < 0)
    diff_info_label_.set_text(std::to_string(diff_ranges_.size()) + " differences");
  else
    diff_info_label_.set_text("Difference " + std::to_string(current_diff_ + 1) +
                              " of " + std::to_string(diff_ranges_.size()));
}

void DiffView::scroll_to_line(int line)
{
  if (line < 0) line = 0;
  auto buf = left_view_.get_buffer();
  auto iter = buf->get_iter_at_line(line);
  // Scroll both views; within_margin 0.0, align 0.2 keeps the target line a
  // little below the top edge.
  left_view_.scroll_to(iter, 0.0, 0.0, 0.2);
  auto riter = right_view_.get_buffer()->get_iter_at_line(line);
  right_view_.scroll_to(riter, 0.0, 0.0, 0.2);
}

void DiffView::jump_to_diff(int idx)
{
  if (diff_ranges_.empty()) return;
  const int n = static_cast<int>(diff_ranges_.size());
  idx = ((idx % n) + n) % n;  // wrap around
  current_diff_ = idx;
  scroll_to_line(diff_ranges_[idx].first);
  update_diff_info_label();
  minimap_.queue_draw();
}

void DiffView::on_prev_diff()
{
  if (diff_ranges_.empty()) return;
  jump_to_diff(current_diff_ < 0 ? static_cast<int>(diff_ranges_.size()) - 1
                                 : current_diff_ - 1);
}

void DiffView::on_next_diff()
{
  if (diff_ranges_.empty()) return;
  jump_to_diff(current_diff_ < 0 ? 0 : current_diff_ + 1);
}

bool DiffView::on_textview_key_press(GdkEventKey* event)
{
  const bool shift = (event->state & GDK_SHIFT_MASK) != 0;
  const bool ctrl  = (event->state & GDK_CONTROL_MASK) != 0;
  const bool alt   = (event->state & GDK_MOD1_MASK) != 0;

  // Ctrl+F: open / re-focus the search bar.
  if (ctrl && !alt && (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
    open_search();
    return true;
  }

  // When the search bar is open, F3/Shift-F3 and Escape operate on search.
  if (search_active_) {
    switch (event->keyval) {
      case GDK_KEY_F3:
        search_jump(shift ? -1 : +1);
        return true;
      case GDK_KEY_Escape:
        close_search();
        return true;
      default:
        break;
    }
  }

  // Ctrl/Alt modifiers — let TextView handle them (copy, select-all, …).
  if (ctrl || alt) return false;

  if (diff_ranges_.empty()) return false;

  switch (event->keyval)
  {
    case GDK_KEY_Tab:
      on_next_diff();
      return true;

    case GDK_KEY_ISO_Left_Tab:  // GDK gives this when Shift+Tab is pressed
      on_prev_diff();
      return true;

    case GDK_KEY_F3:
      if (shift) on_prev_diff(); else on_next_diff();
      return true;

    default:
      return false;
  }
}

bool DiffView::on_minimap_draw(const Cairo::RefPtr<Cairo::Context>& cr)
{
  const int w = minimap_.get_allocated_width();
  const int h = minimap_.get_allocated_height();

  // Background
  cr->set_source_rgb(0.93, 0.93, 0.93);
  cr->rectangle(0, 0, w, h);
  cr->fill();

  if (total_lines_ <= 0 || h <= 0)
    return true;

  // Diff-range markers
  for (std::size_t i = 0; i < diff_ranges_.size(); ++i)
  {
    const auto [start, end] = diff_ranges_[i];
    double y1 = static_cast<double>(start)     / total_lines_ * h;
    double y2 = static_cast<double>(end + 1)   / total_lines_ * h;
    if (y2 - y1 < 2.0) y2 = y1 + 2.0;  // ensure visible for tiny diffs

    if (static_cast<int>(i) == current_diff_)
      cr->set_source_rgb(0.85, 0.25, 0.25);  // highlight selected diff
    else
      cr->set_source_rgb(0.95, 0.55, 0.55);
    cr->rectangle(0, y1, w, y2 - y1);
    cr->fill();
  }

  // Viewport indicator: the portion currently visible in the text views.
  auto vadj = left_scroll_.get_vadjustment();
  if (vadj)
  {
    const double range = vadj->get_upper() - vadj->get_lower();
    const double page  = vadj->get_page_size();
    if (range > 0.0 && page < range)
    {
      const double top = (vadj->get_value() - vadj->get_lower()) / range * h;
      const double ph  = std::max(4.0, page / range * h);
      cr->set_source_rgba(0.2, 0.45, 0.9, 0.18);
      cr->rectangle(0, top, w, ph);
      cr->fill();
      cr->set_source_rgba(0.2, 0.45, 0.9, 0.6);
      cr->set_line_width(1.0);
      cr->rectangle(0.5, top + 0.5, w - 1.0, std::max(1.0, ph - 1.0));
      cr->stroke();
    }
  }

  return true;
}

bool DiffView::on_minimap_button_press(GdkEventButton* event)
{
  if (event->button != 1 || total_lines_ <= 0)
    return false;
  const int h = minimap_.get_allocated_height();
  if (h <= 0) return false;

  const int target_line = std::clamp(
      static_cast<int>(event->y / h * total_lines_), 0, total_lines_ - 1);
  scroll_to_line(target_line);
  return true;
}

// ---------------------------------------------------------------------------
// In-pane text search
// ---------------------------------------------------------------------------

void DiffView::open_search()
{
  search_active_ = true;
  search_bar_.show();
  search_entry_.grab_focus();
  search_entry_.select_region(0, -1);
}

void DiffView::close_search()
{
  search_active_ = false;
  search_bar_.hide();

  auto lbuf = left_view_.get_buffer();
  auto rbuf = right_view_.get_buffer();
  lbuf->remove_tag(tag_search_left_,     lbuf->begin(), lbuf->end());
  lbuf->remove_tag(tag_search_cur_left_, lbuf->begin(), lbuf->end());
  rbuf->remove_tag(tag_search_right_,    rbuf->begin(), rbuf->end());
  rbuf->remove_tag(tag_search_cur_right_,rbuf->begin(), rbuf->end());

  search_hits_.clear();
  current_search_hit_ = -1;

  left_view_.grab_focus();
}

void DiffView::run_search()
{
  auto lbuf = left_view_.get_buffer();
  auto rbuf = right_view_.get_buffer();

  lbuf->remove_tag(tag_search_left_,     lbuf->begin(), lbuf->end());
  lbuf->remove_tag(tag_search_cur_left_, lbuf->begin(), lbuf->end());
  rbuf->remove_tag(tag_search_right_,    rbuf->begin(), rbuf->end());
  rbuf->remove_tag(tag_search_cur_right_,rbuf->begin(), rbuf->end());

  search_hits_.clear();
  current_search_hit_ = -1;

  const Glib::ustring query = search_entry_.get_text();
  if (query.empty()) {
    update_search_info_label();
    return;
  }

  // Collect matches from one buffer and add to search_hits_.
  auto collect = [this, &query](bool left_pane,
                                Glib::RefPtr<Gtk::TextBuffer> buf,
                                const Glib::RefPtr<Gtk::TextBuffer::Tag>& hit_tag)
  {
    Gtk::TextIter ms, me;
    auto it = buf->begin();
    const auto lim = buf->end();
    while (it.forward_search(query, Gtk::TEXT_SEARCH_TEXT_ONLY, ms, me, lim)) {
      search_hits_.push_back({left_pane, ms.get_line(), ms.get_offset(), me.get_offset()});
      buf->apply_tag(hit_tag, ms, me);
      it = me;
    }
  };

  collect(true,  lbuf, tag_search_left_);
  collect(false, rbuf, tag_search_right_);

  // Sort by line, then left pane before right for the same line.
  std::sort(search_hits_.begin(), search_hits_.end(),
    [](const SearchHit& a, const SearchHit& b) {
      if (a.line != b.line)           return a.line < b.line;
      if (a.left_pane != b.left_pane) return a.left_pane > b.left_pane;
      return a.start_offset < b.start_offset;
    });

  if (!search_hits_.empty()) {
    current_search_hit_ = 0;
    update_search_highlights();
  }
  update_search_info_label();
}

void DiffView::update_search_highlights()
{
  auto lbuf = left_view_.get_buffer();
  auto rbuf = right_view_.get_buffer();
  lbuf->remove_tag(tag_search_cur_left_, lbuf->begin(), lbuf->end());
  rbuf->remove_tag(tag_search_cur_right_,rbuf->begin(), rbuf->end());

  if (search_hits_.empty() || current_search_hit_ < 0) return;

  const auto& h   = search_hits_[current_search_hit_];
  auto&       buf = h.left_pane ? lbuf : rbuf;
  auto& cur_tag   = h.left_pane ? tag_search_cur_left_ : tag_search_cur_right_;
  buf->apply_tag(cur_tag,
                 buf->get_iter_at_offset(h.start_offset),
                 buf->get_iter_at_offset(h.end_offset));
  scroll_to_line(h.line);
}

void DiffView::search_jump(int delta)
{
  if (search_hits_.empty()) return;
  const int n = static_cast<int>(search_hits_.size());
  current_search_hit_ = ((current_search_hit_ + delta) % n + n) % n;
  update_search_highlights();
  update_search_info_label();
}

void DiffView::update_search_info_label()
{
  if (search_hits_.empty())
    search_info_label_.set_text(search_entry_.get_text().empty() ? "" : "No matches");
  else
    search_info_label_.set_text(std::to_string(current_search_hit_ + 1) +
                                "/" + std::to_string(search_hits_.size()));
}

bool DiffView::on_search_key_press(GdkEventKey* event)
{
  const bool shift = (event->state & GDK_SHIFT_MASK) != 0;
  switch (event->keyval) {
    case GDK_KEY_Escape:
      close_search();
      return true;
    case GDK_KEY_F3:
      search_jump(shift ? -1 : +1);
      return true;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
      search_jump(shift ? -1 : +1);
      return true;
    default:
      return false;
  }
}
