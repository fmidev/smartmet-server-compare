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

// ---------------------------------------------------------------------------
// DiffView – construction
// ---------------------------------------------------------------------------

DiffView::DiffView() : Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0)
{
  init_column(left_col_, left_label_, left_scroll_, left_view_);
  init_column(right_col_, right_label_, right_scroll_, right_view_);

  pack_start(left_col_, true, true, 0);
  pack_start(right_col_, true, true, 0);

  // Create text buffer tags
  auto lbuf = left_view_.get_buffer();
  auto rbuf = right_view_.get_buffer();

  tag_del_ = lbuf->create_tag("del");
  tag_del_->property_background() = "#ffcccc";

  tag_ins_ = rbuf->create_tag("ins");
  tag_ins_->property_background() = "#ccffcc";

  tag_ph_left_ = lbuf->create_tag("ph");
  tag_ph_left_->property_background() = "#e8e8e8";
  tag_ph_left_->property_foreground() = "#b0b0b0";

  tag_ph_right_ = rbuf->create_tag("ph");
  tag_ph_right_->property_background() = "#e8e8e8";
  tag_ph_right_->property_foreground() = "#b0b0b0";

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
// Text diff using dtl
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

  // GTK text iterators are invalidated by every insert() call.  Use marks
  // instead: a mark is a persistent position that survives buffer mutations.
  // left_gravity=false (right gravity) makes the mark advance past newly
  // inserted text, so it always sits at the current append position.
  auto lmark = lbuf->create_mark(lbuf->end(), false);
  auto rmark = rbuf->create_mark(rbuf->end(), false);

  for (const auto& [elem, info] : d.getSes().getSequence())
  {
    switch (info.type)
    {
      case dtl::SES_COMMON:
        lbuf->insert(lmark->get_iter(), elem + "\n");
        rbuf->insert(rmark->get_iter(), elem + "\n");
        break;

      case dtl::SES_DELETE:
      {
        // Red line on left, grey placeholder on right.
        // Save start offset as integer (immune to invalidation), then insert,
        // then reconstruct both iterators from the now-updated mark.
        int loff = lmark->get_iter().get_offset();
        lbuf->insert(lmark->get_iter(), elem + "\n");
        lbuf->apply_tag(tag_del_,
                        lbuf->get_iter_at_offset(loff),
                        lmark->get_iter());

        int roff = rmark->get_iter().get_offset();
        rbuf->insert(rmark->get_iter(), "\n");
        rbuf->apply_tag(tag_ph_right_,
                        rbuf->get_iter_at_offset(roff),
                        rmark->get_iter());
        break;
      }

      case dtl::SES_ADD:
      {
        // Grey placeholder on left, green line on right.
        int loff = lmark->get_iter().get_offset();
        lbuf->insert(lmark->get_iter(), "\n");
        lbuf->apply_tag(tag_ph_left_,
                        lbuf->get_iter_at_offset(loff),
                        lmark->get_iter());

        int roff = rmark->get_iter().get_offset();
        rbuf->insert(rmark->get_iter(), elem + "\n");
        rbuf->apply_tag(tag_ins_,
                        rbuf->get_iter_at_offset(roff),
                        rmark->get_iter());
        break;
      }
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
