#include "DiffView.h"

#include <dtl/dtl.hpp>

#include <sstream>
#include <string>
#include <vector>

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

// Character-level SES for two strings.
using CharSes = std::vector<std::pair<char, dtl::elemInfo>>;

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

DiffView::DiffView() : Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0)
{
  init_column(left_col_, left_label_, left_scroll_, left_view_);
  init_column(right_col_, right_label_, right_scroll_, right_view_);

  pack_start(left_col_, true, true, 0);
  pack_start(right_col_, true, true, 0);

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

  connect_scroll_sync();
  show_all();
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
  scroll.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);

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
      });

  rva->signal_value_changed().connect(
      [this, lva, rva]()
      {
        if (syncing_scroll_) return;
        syncing_scroll_ = true;
        lva->set_value(rva->get_value());
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

void DiffView::set_texts(const std::string& text1,
                         const std::string& text2,
                         const std::string& label1,
                         const std::string& label2)
{
  set_labels(label1, label2);

  const auto lines1 = split_lines(text1);
  const auto lines2 = split_lines(text2);

  dtl::Diff<std::string, std::vector<std::string>> d(lines1, lines2);
  d.compose();

  auto lbuf = left_view_.get_buffer();
  auto rbuf = right_view_.get_buffer();
  lbuf->set_text("");
  rbuf->set_text("");

  // Right-gravity marks survive every insert() call and advance past new text.
  auto lmark = lbuf->create_mark(lbuf->end(), false);
  auto rmark = rbuf->create_mark(rbuf->end(), false);

  // Walk the line-level SES.  We process COMMON lines immediately.
  // For consecutive DELETE / ADD elements we collect them into a group first,
  // then pair them up for character-level diffing.
  const auto& ses = d.getSes().getSequence();
  std::size_t i = 0;

  while (i < ses.size())
  {
    const auto& [elem, info] = ses[i];

    if (info.type == dtl::SES_COMMON)
    {
      lbuf->insert(lmark->get_iter(), elem + "\n");
      rbuf->insert(rmark->get_iter(), elem + "\n");
      ++i;
      continue;
    }

    // Collect the next contiguous block of non-COMMON elements.
    std::vector<std::string> dels, adds;
    while (i < ses.size() && ses[i].second.type != dtl::SES_COMMON)
    {
      if (ses[i].second.type == dtl::SES_DELETE)
        dels.push_back(ses[i].first);
      else
        adds.push_back(ses[i].first);
      ++i;
    }

    // Pair up deletions and insertions: paired lines get character-level diff.
    const std::size_t pairs = std::min(dels.size(), adds.size());

    for (std::size_t j = 0; j < pairs; ++j)
    {
      const auto cses = char_ses(dels[j], adds[j]);

      insert_line(lbuf, lmark, dels[j], tag_del_, tag_del_char_, &cses, dtl::SES_DELETE);
      insert_line(rbuf, rmark, adds[j], tag_ins_, tag_ins_char_, &cses, dtl::SES_ADD);
    }

    // Unmatched deletions (more DELETEs than ADDs): whole-line highlight only.
    for (std::size_t j = pairs; j < dels.size(); ++j)
    {
      insert_line(lbuf, lmark, dels[j], tag_del_, {}, nullptr, dtl::SES_DELETE);
      insert_placeholder(rbuf, rmark, tag_ph_right_);
    }

    // Unmatched insertions (more ADDs than DELETEs): whole-line highlight only.
    for (std::size_t j = pairs; j < adds.size(); ++j)
    {
      insert_placeholder(lbuf, lmark, tag_ph_left_);
      insert_line(rbuf, rmark, adds[j], tag_ins_, {}, nullptr, dtl::SES_ADD);
    }
  }

  lbuf->delete_mark(lmark);
  rbuf->delete_mark(rmark);
}

// ---------------------------------------------------------------------------
// Non-text / error helpers
// ---------------------------------------------------------------------------

void DiffView::set_binary(bool equal, const std::string& label1, const std::string& label2)
{
  set_labels(label1, label2);
  const char* msg = equal ? "[binary content – identical]" : "[binary content – DIFFERENT]";
  left_view_.get_buffer()->set_text(msg);
  right_view_.get_buffer()->set_text(msg);
}

void DiffView::set_error(const std::string& msg1,
                         const std::string& msg2,
                         const std::string& label1,
                         const std::string& label2)
{
  set_labels(label1, label2);
  left_view_.get_buffer()->set_text(msg1.empty() ? "(no error)" : msg1);
  right_view_.get_buffer()->set_text(msg2.empty() ? "(no error)" : msg2);
}

void DiffView::clear()
{
  left_label_.set_text("  Server 1");
  right_label_.set_text("  Server 2");
  left_view_.get_buffer()->set_text("");
  right_view_.get_buffer()->set_text("");
}
