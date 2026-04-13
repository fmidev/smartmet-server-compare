#include "StatusPanel.h"

StatusPanel::StatusPanel() : Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6)
{
  progress_.set_show_text(true);
  progress_.set_fraction(0.0);
  progress_.set_size_request(180, -1);

  set_border_width(2);
  pack_start(progress_, false, false);
  pack_start(statusbar_, true, true);
}

void StatusPanel::set_status(const Glib::ustring& msg)
{
  statusbar_.pop();
  statusbar_.push(msg);
}

void StatusPanel::set_progress(double fraction)
{
  progress_.set_fraction(fraction);
}

void StatusPanel::pulse()
{
  progress_.pulse();
}
