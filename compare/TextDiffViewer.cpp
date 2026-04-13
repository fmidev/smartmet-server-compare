#include "TextDiffViewer.h"

bool TextDiffViewer::can_handle(const CompareResult& result) const
{
  // Catch-all: any final state.  ResultPanel ensures we are not called for
  // PENDING / RUNNING.
  (void)result;
  return true;
}

void TextDiffViewer::show(const CompareResult& result,
                          const std::string& label1,
                          const std::string& label2)
{
  if (result.status == CompareStatus::ERROR ||
      result.status == CompareStatus::TOO_LARGE)
  {
    diff_.set_error(result.error1, result.error2, label1, label2);
    return;
  }

  const bool has_text = !result.formatted1.empty() || !result.formatted2.empty();
  if (has_text)
    diff_.set_texts(result.formatted1, result.formatted2, label1, label2);
  else
    diff_.set_binary(result.status == CompareStatus::EQUAL, label1, label2);
}
