#pragma once
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/scrollbar.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/separator.h>
#include <gtkmm/textview.h>

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * Side-by-side text diff widget styled after kdiff3's central comparison pane.
 *
 * Both panes always have the same line count: deleted lines are shown with a
 * grey placeholder in the right pane and vice-versa, so vertical scrolling is
 * perfectly synchronised.
 *
 * Colour coding:
 *   - Equal lines             : default background
 *   - Deleted lines           : light-red background (left) / grey placeholder (right)
 *   - Inserted lines          : grey placeholder (left) / light-green background (right)
 *   - Changed char runs       : darker red / darker green overlaid on the line colour
 *     (paired DELETE+ADD lines get a character-level diff so only the actually
 *      different characters are highlighted with the stronger colour)
 *
 * The diff is computed with the dtl (Diff Template Library).
 */
class DiffView : public Gtk::Box
{
 public:
  DiffView();

  // Opaque precomputed diff data.  Produced by compute_diff() on a worker
  // thread and consumed by apply_prepared() on the main thread.
  struct PreparedDiff;

  // Run the line + paired-character SES computation.  Safe to call from a
  // background thread; poll `cancel_token.load()` and bail out early by
  // returning an empty shared_ptr.
  static std::shared_ptr<PreparedDiff> compute_diff(
      const std::string& text1,
      const std::string& text2,
      const std::atomic<bool>& cancel_token);

  // Apply a PreparedDiff to the two panes.  Main-thread only.  Passing a
  // null pointer clears the panes.
  void apply_prepared(const std::shared_ptr<PreparedDiff>& prepared,
                      const std::string& label1 = "Server 1",
                      const std::string& label2 = "Server 2");

  // Replace both panes with a diff of text1 vs text2.  Convenience wrapper
  // that runs compute_diff() + apply_prepared() synchronously on the
  // current thread.  May block for large texts.
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

  // Diff navigation helpers (valid only after set_texts() has built the diff).
  void reset_diff_navigation();
  void scroll_to_line(int line);
  void jump_to_diff(int idx);
  void on_prev_diff();
  void on_next_diff();
  void update_diff_info_label();
  bool on_minimap_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_minimap_button_press(GdkEventButton* event);

  // Tab / Shift-Tab / F3 / Shift-F3 keyboard navigation; runs BEFORE the
  // TextView's default handler so Tab isn't swallowed for focus traversal.
  bool on_textview_key_press(GdkEventKey* event);

  // In-pane text search (Ctrl+F to open, Escape to close, F3 to navigate).
  void open_search();
  void close_search();
  void run_search();
  void update_search_highlights();
  void search_jump(int delta);
  void update_search_info_label();
  bool on_search_key_press(GdkEventKey* event);

  Gtk::Box nav_row_{Gtk::ORIENTATION_HORIZONTAL, 4};
  Gtk::Button btn_prev_diff_{"Prev diff"};
  Gtk::Button btn_next_diff_{"Next diff"};
  Gtk::Label diff_info_label_;

  // Search bar (shown on Ctrl+F, hidden on Escape / close button).
  Gtk::Box         search_bar_{Gtk::ORIENTATION_HORIZONTAL, 4};
  Gtk::SearchEntry search_entry_;
  Gtk::Button      btn_search_prev_{"◀"};
  Gtk::Button      btn_search_next_{"▶"};
  Gtk::Label       search_info_label_;

  Gtk::Box h_box_{Gtk::ORIENTATION_HORIZONTAL, 0};
  Gtk::Box left_col_{Gtk::ORIENTATION_VERTICAL, 0};
  Gtk::Box right_col_{Gtk::ORIENTATION_VERTICAL, 0};

  Gtk::Separator vseparator_{Gtk::ORIENTATION_VERTICAL};

  Gtk::DrawingArea minimap_;

  Gtk::Scrollbar hscrollbar_;
  Glib::RefPtr<Gtk::Adjustment> shared_hadj_;

  Gtk::Label left_label_;
  Gtk::Label right_label_;

  Gtk::ScrolledWindow left_scroll_;
  Gtk::ScrolledWindow right_scroll_;

  Gtk::TextView left_view_;
  Gtk::TextView right_view_;

  // Differences tracked during set_texts(): (first_line, last_line) inclusive
  // in absolute buffer-line numbers.  Both panes share the same line count.
  std::vector<std::pair<int, int>> diff_ranges_;
  int total_lines_ = 0;
  int current_diff_ = -1;

  // Text buffer tags (created once in the constructor).
  // Line-level tags are created first so char-level tags get higher priority
  // in the tag table and override the line background on individual characters.
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_del_;        // light-red bg, whole deleted line (left)
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_ins_;        // light-green bg, whole inserted line (right)
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_ph_left_;    // grey placeholder line (left)
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_ph_right_;   // grey placeholder line (right)
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_del_char_;   // darker-red, changed char runs (left)
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_ins_char_;   // darker-green, changed char runs (right)
  // Search highlight tags – created after diff tags so they take priority.
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_search_left_;
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_search_right_;
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_search_cur_left_;
  Glib::RefPtr<Gtk::TextBuffer::Tag> tag_search_cur_right_;

  // Search state.
  struct SearchHit { bool left_pane; int line; int start_offset; int end_offset; };
  std::vector<SearchHit> search_hits_;
  int  current_search_hit_ = -1;
  bool search_active_      = false;

  bool syncing_scroll_{false};
};
