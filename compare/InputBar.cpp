#include "InputBar.h"

#include <gtkmm/entry.h>

InputBar::InputBar(Settings& settings)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0), settings_(settings)
{
  // ---- Row 1 ----
  lbl_source_.set_xalign(1.0f);
  setup_combo_entry(ent_source_, "http://host:port", 30);

  lbl_prefix_.set_xalign(1.0f);
  setup_combo_entry(ent_prefix_, "/wfs", 10);

  lbl_minutes_.set_xalign(1.0f);
  spin_minutes_.set_range(1, 1440);
  spin_minutes_.set_increments(1, 10);
  spin_minutes_.set_width_chars(5);

  btn_fetch_.signal_clicked().connect(sigc::mem_fun(*this, &InputBar::on_fetch_clicked));
  btn_load_file_.signal_clicked().connect(sigc::mem_fun(*this, &InputBar::on_load_clicked));
  btn_save_file_.signal_clicked().connect(sigc::mem_fun(*this, &InputBar::on_save_clicked));

  btn_save_file_.set_tooltip_text(
      "Write the request strings of all currently visible (filtered) rows "
      "to a text file that 'Load from file' can re-read.");

  row1_.set_border_width(4);
  row1_.pack_start(lbl_source_, false, false);
  row1_.pack_start(ent_source_, true, true);
  row1_.pack_start(lbl_prefix_, false, false);
  row1_.pack_start(ent_prefix_, false, false);
  row1_.pack_start(lbl_minutes_, false, false);
  row1_.pack_start(spin_minutes_, false, false);
  row1_.pack_start(btn_fetch_, false, false);
  row1_.pack_start(btn_load_file_, false, false);
  row1_.pack_start(btn_save_file_, false, false);

  // ---- Row 2 ----
  lbl_srv1_.set_xalign(1.0f);
  setup_combo_entry(ent_srv1_, "http://server1:port", 28);

  lbl_srv2_.set_xalign(1.0f);
  setup_combo_entry(ent_srv2_, "http://server2:port", 28);

  lbl_concurrent_.set_xalign(1.0f);
  spin_concurrent_.set_range(1, 32);
  spin_concurrent_.set_increments(1, 4);
  spin_concurrent_.set_width_chars(4);

  lbl_max_size_.set_xalign(1.0f);
  spin_max_size_.set_range(0, 1024);
  spin_max_size_.set_increments(1, 10);
  spin_max_size_.set_width_chars(4);

  btn_compare_.signal_clicked().connect(sigc::mem_fun(*this, &InputBar::on_compare_clicked));
  btn_rerun_filtered_.signal_clicked().connect(
      sigc::mem_fun(*this, &InputBar::on_rerun_filtered_clicked));
  btn_stop_.signal_clicked().connect(sigc::mem_fun(*this, &InputBar::on_stop_clicked));

  btn_rerun_filtered_.set_tooltip_text(
      "Replace the request list with the currently visible (filtered) rows "
      "and re-run the comparison on them.");

  row2_.set_border_width(4);
  row2_.pack_start(lbl_srv1_, false, false);
  row2_.pack_start(ent_srv1_, true, true);
  row2_.pack_start(lbl_srv2_, false, false);
  row2_.pack_start(ent_srv2_, true, true);
  row2_.pack_start(lbl_concurrent_, false, false);
  row2_.pack_start(spin_concurrent_, false, false);
  row2_.pack_start(lbl_max_size_, false, false);
  row2_.pack_start(spin_max_size_, false, false);
  row2_.pack_start(btn_compare_, false, false);
  row2_.pack_start(btn_rerun_filtered_, false, false);
  row2_.pack_start(btn_stop_, false, false);

  pack_start(row1_, false, false);
  pack_start(row2_, false, false);

  // ---- Initial values from Settings ----
  load_combo(ent_source_, "source_server");
  load_combo(ent_prefix_, "prefix");
  load_combo(ent_srv1_,   "server1");
  load_combo(ent_srv2_,   "server2");
  spin_minutes_.set_value(settings_.get_int("minutes", 2));
  spin_concurrent_.set_value(settings_.get_int("max_concurrent", 4));
  spin_max_size_.set_value(settings_.get_int("max_size_mb", 10));

  // Initial sensitivity: empty list, nothing running.
  set_idle(false);
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

std::string InputBar::source_url()  const { return combo_text(ent_source_); }
std::string InputBar::prefix()      const { return combo_text(ent_prefix_); }
int         InputBar::minutes()     const { return static_cast<int>(spin_minutes_.get_value()); }
std::string InputBar::server1_url() const { return combo_text(ent_srv1_); }
std::string InputBar::server2_url() const { return combo_text(ent_srv2_); }
int         InputBar::max_concurrent() const { return static_cast<int>(spin_concurrent_.get_value()); }
std::size_t InputBar::max_size_mb()    const { return static_cast<std::size_t>(spin_max_size_.get_value()); }

// ---------------------------------------------------------------------------
// Sensitivity transitions
// ---------------------------------------------------------------------------

void InputBar::set_idle(bool has_queries)
{
  btn_fetch_.set_sensitive(true);
  btn_load_file_.set_sensitive(true);
  btn_save_file_.set_sensitive(has_queries);
  btn_compare_.set_sensitive(has_queries);
  btn_rerun_filtered_.set_sensitive(has_queries);
  btn_stop_.set_sensitive(false);
}

void InputBar::start_fetching()
{
  btn_fetch_.set_sensitive(false);
  btn_compare_.set_sensitive(false);
  btn_rerun_filtered_.set_sensitive(false);
  btn_save_file_.set_sensitive(false);
  // Load and Stop unchanged (Stop should already be off here).
}

void InputBar::start_comparing()
{
  btn_fetch_.set_sensitive(false);
  btn_load_file_.set_sensitive(false);
  btn_compare_.set_sensitive(false);
  btn_rerun_filtered_.set_sensitive(false);
  // Save stays enabled — the request list isn't mutated during a compare,
  // so it's safe (and useful) to dump the current filter view to disk.
  btn_stop_.set_sensitive(true);
}

void InputBar::notify_stopping()
{
  btn_stop_.set_sensitive(false);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void InputBar::save_fetch_inputs()
{
  save_combo(ent_source_, "source_server");
  save_combo(ent_prefix_, "prefix");
  settings_.set_int("minutes", minutes());
}

void InputBar::save_compare_inputs()
{
  save_combo(ent_srv1_, "server1");
  save_combo(ent_srv2_, "server2");
  settings_.set_int("max_concurrent", max_concurrent());
  settings_.set_int("max_size_mb", static_cast<int>(max_size_mb()));
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/* static */
std::string InputBar::combo_text(const Gtk::ComboBoxText& combo)
{
  return combo.get_active_text();
}

/* static */
void InputBar::setup_combo_entry(Gtk::ComboBoxText& combo,
                                 const char* placeholder,
                                 int width_chars)
{
  if (auto* e = dynamic_cast<Gtk::Entry*>(combo.get_child()))
  {
    e->set_placeholder_text(placeholder);
    e->set_width_chars(width_chars);
  }
}

void InputBar::load_combo(Gtk::ComboBoxText& combo, const std::string& key)
{
  const auto items = settings_.history(key);
  combo.remove_all();
  for (const auto& item : items)
    combo.append(item);

  if (!items.empty())
    if (auto* e = dynamic_cast<Gtk::Entry*>(combo.get_child()))
      e->set_text(items.front());
}

void InputBar::save_combo(Gtk::ComboBoxText& combo, const std::string& key)
{
  const std::string val = combo_text(combo);
  if (val.empty())
    return;
  settings_.add_to_history(key, val);
  load_combo(combo, key);
}

// ---------------------------------------------------------------------------
// Click handlers — persist then re-emit as a typed signal
// ---------------------------------------------------------------------------

void InputBar::on_fetch_clicked()
{
  save_fetch_inputs();
  sig_fetch_.emit();
}

void InputBar::on_load_clicked()
{
  sig_load_.emit();
}

void InputBar::on_save_clicked()
{
  sig_save_.emit();
}

void InputBar::on_compare_clicked()
{
  save_compare_inputs();
  sig_compare_.emit();
}

void InputBar::on_rerun_filtered_clicked()
{
  save_compare_inputs();
  sig_rerun_filtered_.emit();
}

void InputBar::on_stop_clicked()
{
  sig_stop_.emit();
}
