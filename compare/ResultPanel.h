#pragma once
#include "ResultViewer.h"
#include "Types.h"

#include <gtkmm/stack.h>

#include <memory>
#include <string>
#include <vector>

/**
 * Container that holds one or more ResultViewer plug-ins and routes each
 * incoming CompareResult to the first viewer whose can_handle() returns true.
 *
 * Use add_viewer() in registration order from most- to least-specific; the
 * generic text/binary fallback (TextDiffViewer) goes last.
 *
 * Implemented as a Gtk::Stack — only the active viewer's widget is visible.
 */
class ResultPanel : public Gtk::Stack
{
 public:
  ResultPanel();

  // Take ownership of `viewer` and add its widget as a stack page.
  void add_viewer(std::unique_ptr<ResultViewer> viewer);

  // Display `result`.  PENDING/RUNNING and "no viewer can handle this" both
  // clear the active viewer.  `label1`/`label2` are usually the server URLs.
  void show(const CompareResult& result,
            const std::string& label1,
            const std::string& label2);

  // Clear every registered viewer and leave the panel blank.
  void clear();

 private:
  std::vector<std::unique_ptr<ResultViewer>> viewers_;
};
