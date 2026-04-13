#pragma once
#include "Types.h"

#include <gtkmm/widget.h>

#include <string>

/**
 * Abstract interface for a widget that can display the comparison result for
 * one query.  Concrete viewers handle particular content kinds (text diff,
 * image diff, …) and are registered with ResultPanel.
 *
 * ResultPanel walks its registered viewers in order and uses the first one
 * whose can_handle() returns true for a given result, so a catch-all (e.g.
 * the text/binary fallback) should be registered last.
 *
 * Viewers do not own a Gtk widget by inheritance; they expose the widget to
 * embed via widget().  This keeps the interface simple and avoids forcing a
 * specific Gtk base class on every implementation.
 */
class ResultViewer
{
 public:
  virtual ~ResultViewer() = default;

  // Stable identifier used as the Gtk::Stack child name.
  virtual const char* name() const = 0;

  // Return true if this viewer is willing to display `result`.  Called only
  // for results in a "ready to show" state (not PENDING / not RUNNING).
  virtual bool can_handle(const CompareResult& result) const = 0;

  // Render `result` into the viewer.  `label1` and `label2` are the two
  // server URLs (used for column headings).
  virtual void show(const CompareResult& result,
                    const std::string& label1,
                    const std::string& label2) = 0;

  // Reset to the empty state.
  virtual void clear() = 0;

  // The widget that ResultPanel embeds in its Gtk::Stack.
  virtual Gtk::Widget& widget() = 0;
};
