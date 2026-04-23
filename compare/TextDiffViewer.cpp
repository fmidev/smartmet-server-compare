#include "TextDiffViewer.h"

bool TextDiffViewer::can_handle(const CompareResult& result) const
{
  // Catch-all: any final state.  ResultPanel ensures we are not called for
  // PENDING / RUNNING.
  (void)result;
  return true;
}

std::shared_ptr<void> TextDiffViewer::prepare(const CompareResult& result,
                                               const std::atomic<bool>& cancel_token)
{
  // Only the side-by-side text diff involves heavy work; errors and
  // binary-equal/different rendering is trivial and handled inline in show().
  if (result.status == CompareStatus::ERROR ||
      result.status == CompareStatus::TOO_LARGE)
    return std::shared_ptr<void>{};

  const bool has_text = !result.formatted1.empty() || !result.formatted2.empty();
  if (!has_text)
    return std::shared_ptr<void>{};

  auto diff = DiffView::compute_diff(result.formatted1, result.formatted2, cancel_token);
  if (!diff)
    return std::shared_ptr<void>{};
  // Cast down to the type-erased void via shared_ptr aliasing.
  return std::static_pointer_cast<void>(diff);
}

void TextDiffViewer::show(const CompareResult& result,
                          const std::string& label1,
                          const std::string& label2,
                          std::shared_ptr<void> prepared)
{
  if (result.status == CompareStatus::ERROR ||
      result.status == CompareStatus::TOO_LARGE)
  {
    diff_.set_error(result.error1, result.error2, label1, label2);
    return;
  }

  const bool has_text = !result.formatted1.empty() || !result.formatted2.empty();
  if (!has_text)
  {
    diff_.set_binary(result.status == CompareStatus::EQUAL, label1, label2);
    return;
  }

  if (prepared)
  {
    auto diff = std::static_pointer_cast<DiffView::PreparedDiff>(prepared);
    diff_.apply_prepared(diff, label1, label2);
  }
  else
  {
    diff_.set_texts(result.formatted1, result.formatted2, label1, label2);
  }
}

void TextDiffViewer::show(const CompareResult& result,
                          const std::string& label1,
                          const std::string& label2)
{
  show(result, label1, label2, std::shared_ptr<void>{});
}
