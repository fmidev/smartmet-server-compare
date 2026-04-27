#pragma once
#include "Settings.h"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <sigc++/sigc++.h>

#include <cstddef>
#include <string>

/**
 * Two-row input panel above the request list:
 *   Row 1: source URL, prefix filter, minutes, [Fetch queries] [Load from file]
 *   Row 2: server1, server2, max concurrent, max size, [Compare all] [Stop]
 *
 * Reads initial values from `settings` at construction time and writes them
 * back when the user triggers fetch / compare (so the next launch remembers
 * the latest values even on a failed run).
 *
 * Emits one signal per button.  The MainWindow connects to these and is
 * responsible for the actual work.
 */
class InputBar : public Gtk::Box
{
 public:
  explicit InputBar(Settings& settings);

  // ----- Current input values -----
  std::string source_url()  const;
  std::string prefix()      const;
  int         minutes()     const;
  std::string server1_url() const;
  std::string server2_url() const;
  int         max_concurrent() const;
  std::size_t max_size_mb()    const;

  // ----- Button-state transitions -----
  // Idle = nothing running; compare button enabled iff we have queries to run.
  void set_idle(bool has_queries);
  // Fetch in progress: disable Fetch + Compare; Load and Stop unchanged.
  void start_fetching();
  // Comparison running: disable everything except Stop.
  void start_comparing();
  // Stop has been clicked: keep "comparing" disabled set, but also disable
  // Stop until the worker truly finishes.
  void notify_stopping();

  // Persist the current combo / spinbutton values to Settings.  Called
  // automatically before a fetch/compare signal is emitted, but exposed in
  // case callers want to force a save.
  void save_fetch_inputs();
  void save_compare_inputs();

  // ----- Signals -----
  sigc::signal<void()>& signal_fetch()      { return sig_fetch_; }
  sigc::signal<void()>& signal_load_file()  { return sig_load_; }
  sigc::signal<void()>& signal_save_file()  { return sig_save_; }
  sigc::signal<void()>& signal_compare()    { return sig_compare_; }
  sigc::signal<void()>& signal_stop()       { return sig_stop_; }

 private:
  void load_combo(Gtk::ComboBoxText& combo, const std::string& key);
  void save_combo(Gtk::ComboBoxText& combo, const std::string& key);

  static std::string combo_text(const Gtk::ComboBoxText& combo);
  static void setup_combo_entry(Gtk::ComboBoxText& combo,
                                const char* placeholder,
                                int width_chars);

  void on_fetch_clicked();
  void on_load_clicked();
  void on_save_clicked();
  void on_compare_clicked();
  void on_stop_clicked();

  Settings& settings_;

  // Row 1: source server
  Gtk::Box   row1_{Gtk::ORIENTATION_HORIZONTAL, 6};
  Gtk::Label lbl_source_{"Source server:"};
  Gtk::ComboBoxText ent_source_{true};
  Gtk::Label lbl_prefix_{"Prefix filter:"};
  Gtk::ComboBoxText ent_prefix_{true};
  Gtk::Label lbl_minutes_{"Minutes:"};
  Gtk::SpinButton spin_minutes_;
  Gtk::Button btn_fetch_{"Fetch queries"};
  Gtk::Button btn_load_file_{"Load from file…"};
  Gtk::Button btn_save_file_{"Save filtered…"};

  // Row 2: target servers
  Gtk::Box   row2_{Gtk::ORIENTATION_HORIZONTAL, 6};
  Gtk::Label lbl_srv1_{"Server 1:"};
  Gtk::ComboBoxText ent_srv1_{true};
  Gtk::Label lbl_srv2_{"Server 2:"};
  Gtk::ComboBoxText ent_srv2_{true};
  Gtk::Label lbl_concurrent_{"Max concurrent:"};
  Gtk::SpinButton spin_concurrent_;
  Gtk::Label lbl_max_size_{"Max size (MB):"};
  Gtk::SpinButton spin_max_size_;
  Gtk::Button btn_compare_{"Compare all"};
  Gtk::Button btn_stop_{"Stop"};

  sigc::signal<void()> sig_fetch_;
  sigc::signal<void()> sig_load_;
  sigc::signal<void()> sig_save_;
  sigc::signal<void()> sig_compare_;
  sigc::signal<void()> sig_stop_;
};
