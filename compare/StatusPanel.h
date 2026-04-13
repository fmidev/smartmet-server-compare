#pragma once
#include <gtkmm/box.h>
#include <gtkmm/progressbar.h>
#include <gtkmm/statusbar.h>

/**
 * Bottom strip of the main window: progress bar on the left, single-message
 * status bar on the right.  Stateless wrapper around the two widgets.
 */
class StatusPanel : public Gtk::Box
{
 public:
  StatusPanel();

  // Replace the current status message.
  void set_status(const Glib::ustring& msg);

  // 0.0 .. 1.0 — set determinate progress.
  void set_progress(double fraction);

  // Indeterminate "still working" animation step.
  void pulse();

 private:
  Gtk::ProgressBar progress_;
  Gtk::Statusbar   statusbar_;
};
