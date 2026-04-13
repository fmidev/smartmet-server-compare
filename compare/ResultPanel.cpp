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

void ResultPanel::clear()
{
  for (auto& v : viewers_)
    v->clear();
}
