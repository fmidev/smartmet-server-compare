#pragma once
#include "DiffView.h"
#include "ResultViewer.h"

/**
 * Catch-all ResultViewer that renders results using DiffView:
 *   - ERROR / TOO_LARGE        → side-by-side error messages
 *   - any pretty-printable text → side-by-side text diff
 *   - byte-only equal/different → "binary equal" / "binary different" indicator
 *
 * Acts as the fallback viewer when no more specific (e.g. image) viewer
 * claims the result, so its can_handle() returns true for every non-PENDING /
 * non-RUNNING result.
 */
class TextDiffViewer : public ResultViewer
{
 public:
  const char* name() const override { return "text-diff"; }

  bool can_handle(const CompareResult& result) const override;

  // Off-thread: runs DiffView::compute_diff for results that will render
  // as a side-by-side text diff.  Returns nullptr for non-diff cases
  // (error / binary-only) so show() handles them synchronously.
  std::shared_ptr<void> prepare(const CompareResult& result,
                                 const std::atomic<bool>& cancel_token) override;

  // Fast path for results whose heavy work is already precomputed.
  using ResultViewer::show;
  void show(const CompareResult& result,
            const std::string& label1,
            const std::string& label2,
            std::shared_ptr<void> prepared) override;

  void show(const CompareResult& result,
            const std::string& label1,
            const std::string& label2) override;

  void clear() override { diff_.clear(); }

  Gtk::Widget& widget() override { return diff_; }

 private:
  DiffView diff_;
};
