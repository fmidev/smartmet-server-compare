#pragma once
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>

#include <string>

/**
 * Side-by-side text diff widget styled after kdiff3's central comparison pane.
 *
 * Both panes always have the same line count: deleted lines are shown with a
 * grey placeholder in the right pane and vice-versa, so vertical scrolling is
 * perfectly synchronised.
 *
 * Colour coding:
 *   - Equal lines    : default background
 *   - Deleted lines  : red background (left) / grey placeholder (right)
 *   - Inserted lines : grey placeholder (left) / green background (right)
 *
 * The diff is computed with the dtl (Diff Template Library).
 */
class DiffView : public Gtk::Box
{
 public:
  DiffView();

  // Replace both panes with a diff of text1 vs text2.
  void set_texts(const std::string& text1,
                 const std::string& text2,
                 const std::string& label1 = "Server 1",
                 const std::string& label2 = "Server 2");

  // Show a plain (non-text) equal / not-equal indicator.
  void set_binary(bool equal,
                  const std::string& label1 = "Server 1",
                  const std::string& label2 = "Server 2");

  // Show an error message in one or both panes.
  void set_error(const std::string& msg1,
                 const std::string& msg2,
                 const std::string& label1 = "Server 1",
                 const std::string& label2 = "Server 2");

  // Clear both panes.
  void clear();

 private:
  void init_column(Gtk::Box& col,
                   Gtk::Label& lbl,
                   Gtk::ScrolledWindow& scroll,
                   Gtk::TextView& view);

  void connect_scroll_sync();

  void set_labels(const std::string& label1, const std::string& label2);

  Gtk::Box left_col_{Gtk::ORIENTATION_VERTICAL, 0};
  Gtk::Box right_col_{Gtk::ORIENTATION_VERTICAL, 0};

  Gtk::Label left_label_;
  Gtk::Label right_label_;

  Gtk::ScrolledWindow left_scroll_;
  Gtk::ScrolledWindow right_scroll_;

  Gtk::TextView left_view_;
  Gtk::TextView right_view_;

  // Text buffer tags (created once in the constructor)
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_del_;        // red bg, left pane
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_ins_;        // green bg, right pane
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_ph_left_;    // grey placeholder, left
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_ph_right_;   // grey placeholder, right

  bool syncing_scroll_{false};
};
