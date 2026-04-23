#include "ResultPanel.h"

ResultPanel::ResultPanel()
{
  set_transition_type(Gtk::STACK_TRANSITION_TYPE_NONE);
}

void ResultPanel::add_viewer(std::unique_ptr<ResultViewer> viewer)
{
  add(viewer->widget(), viewer->name());
  viewers_.push_back(std::move(viewer));
}

void ResultPanel::show(const CompareResult& result,
                       const std::string& label1,
                       const std::string& label2)
{
  if (result.status == CompareStatus::PENDING ||
      result.status == CompareStatus::RUNNING)
  {
    clear();
    return;
  }

  for (auto& v : viewers_)
  {
    if (v->can_handle(result))
    {
      v->show(result, label1, label2);
      set_visible_child(v->widget());
      return;
    }
  }

  // No viewer claimed it — clear everything.
  clear();
}

std::pair<ResultViewer*, std::shared_ptr<void>>
ResultPanel::prepare_async(const CompareResult& result,
                            const std::atomic<bool>& cancel_token)
{
  if (result.status == CompareStatus::PENDING ||
      result.status == CompareStatus::RUNNING)
    return {nullptr, std::shared_ptr<void>{}};

  for (auto& v : viewers_)
  {
    if (v->can_handle(result))
    {
      auto prep = v->prepare(result, cancel_token);
      return {v.get(), std::move(prep)};
    }
  }
  return {nullptr, std::shared_ptr<void>{}};
}

void ResultPanel::show_prepared(ResultViewer* viewer,
                                 const CompareResult& result,
                                 const std::string& label1,
                                 const std::string& label2,
                                 std::shared_ptr<void> prepared)
{
  if (!viewer)
  {
    clear();
    return;
  }
  viewer->show(result, label1, label2, std::move(prepared));
  set_visible_child(viewer->widget());
}

void ResultPanel::clear()
{
  for (auto& v : viewers_)
    v->clear();
}
