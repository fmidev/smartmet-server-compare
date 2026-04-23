#pragma once
#include "ResultViewer.h"
#include "Types.h"

#include <gtkmm/stack.h>

#include <atomic>
#include <memory>
#include <string>
#include <utility>
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
  // Synchronous: picks a viewer and runs show() inline — may block for
  // large diffs / image decoding.
  void show(const CompareResult& result,
            const std::string& label1,
            const std::string& label2);

  // Worker-thread phase of the async show pipeline.  Picks the viewer that
  // can_handle() the result, calls its prepare() with `cancel_token`, and
  // returns the pair so the caller can pass both into show_prepared() once
  // back on the main thread.  Returns {nullptr, nullptr} for results that
  // no viewer wants to handle (e.g. PENDING/RUNNING).  Safe to call off the
  // main thread; viewer->prepare() implementations are expected to be too.
  std::pair<ResultViewer*, std::shared_ptr<void>> prepare_async(
      const CompareResult& result,
      const std::atomic<bool>& cancel_token);

  // Main-thread phase: apply whatever prepare_async() produced and flip the
  // stack to that viewer.  A null `viewer` clears the panel.
  void show_prepared(ResultViewer* viewer,
                     const CompareResult& result,
                     const std::string& label1,
                     const std::string& label2,
                     std::shared_ptr<void> prepared);

  // Clear every registered viewer and leave the panel blank.
  void clear();

 private:
  std::vector<std::unique_ptr<ResultViewer>> viewers_;
};
