#include "EditQueryDialog.h"
#include "UrlUtils.h"

#include <gtkmm/separator.h>

#include <algorithm>
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

EditQueryDialog::EditQueryDialog(Gtk::Window& parent,
                                 const std::string& request_string)
    : Gtk::Dialog("Edit Query", parent, true)
{
  set_default_size(760, 520);
  set_modal(true);

  add_button("Discard",   Gtk::RESPONSE_CANCEL);
  add_button("Add after", RESPONSE_ADD_AFTER);
  add_button("Replace",   RESPONSE_REPLACE);
  set_default_response(RESPONSE_REPLACE);

  auto* ca = get_content_area();
  ca->set_spacing(6);
  ca->set_border_width(10);

  // ---- Path row ----
  auto* path_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  auto* path_lbl = Gtk::manage(new Gtk::Label("Path:"));
  path_lbl->set_xalign(1.0f);
  path_lbl->set_size_request(80, -1);
  path_entry_.set_hexpand(true);
  path_entry_.signal_changed().connect(
      sigc::mem_fun(*this, &EditQueryDialog::update_preview));
  path_row->pack_start(*path_lbl,   false, false);
  path_row->pack_start(path_entry_, true,  true);
  ca->pack_start(*path_row, false, false);

  ca->pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL)),
                 false, false, 2);

  // ---- Parameters section header ----
  auto* ph_row  = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  auto* ph_lbl  = Gtk::manage(new Gtk::Label("Parameters:"));
  ph_lbl->set_xalign(0.0f);
  auto* add_btn = Gtk::manage(new Gtk::Button("Add parameter"));
  add_btn->signal_clicked().connect([this] { add_param_row(); });
  ph_row->pack_start(*ph_lbl,  true,  true);
  ph_row->pack_start(*add_btn, false, false);
  ca->pack_start(*ph_row, false, false);

  // ---- Scrollable parameter list ----
  params_scroll_.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  params_scroll_.set_size_request(-1, 240);
  params_scroll_.add(params_box_);
  ca->pack_start(params_scroll_, true, true);

  ca->pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL)),
                 false, false, 2);

  // ---- URL preview ----
  auto* prev_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  auto* prev_lbl = Gtk::manage(new Gtk::Label("URL preview:"));
  prev_lbl->set_xalign(1.0f);
  prev_lbl->set_size_request(80, -1);
  preview_entry_.set_editable(false);
  preview_entry_.set_hexpand(true);
  prev_row->pack_start(*prev_lbl,      false, false);
  prev_row->pack_start(preview_entry_, true,  true);
  ca->pack_start(*prev_row, false, false);

  parse_and_populate(request_string);
  show_all_children();
}

// ---------------------------------------------------------------------------
// Parsing and population
// ---------------------------------------------------------------------------

void EditQueryDialog::parse_and_populate(const std::string& request)
{
  const auto q = request.find('?');
  if (q == std::string::npos)
  {
    path_entry_.set_text(request);
    update_preview();
    return;
  }

  path_entry_.set_text(request.substr(0, q));

  std::istringstream ss(request.substr(q + 1));
  std::string token;
  while (std::getline(ss, token, '&'))
  {
    if (token.empty())
      continue;
    const auto eq = token.find('=');
    if (eq == std::string::npos)
      add_param_row(urldecode(token), {});
    else
      add_param_row(urldecode(token.substr(0, eq)),
                    urldecode(token.substr(eq + 1)));
  }

  update_preview();
}

// ---------------------------------------------------------------------------
// Parameter row management
// ---------------------------------------------------------------------------

void EditQueryDialog::add_param_row(const std::string& key,
                                    const std::string& val)
{
  auto* row     = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 4));
  auto* key_e   = Gtk::manage(new Gtk::Entry());
  auto* eq_lbl  = Gtk::manage(new Gtk::Label("="));
  auto* val_e   = Gtk::manage(new Gtk::Entry());
  auto* del_btn = Gtk::manage(new Gtk::Button("✕"));

  key_e->set_text(key);
  key_e->set_size_request(220, -1);
  key_e->signal_changed().connect(
      sigc::mem_fun(*this, &EditQueryDialog::update_preview));

  val_e->set_text(val);
  val_e->set_hexpand(true);
  val_e->signal_changed().connect(
      sigc::mem_fun(*this, &EditQueryDialog::update_preview));

  del_btn->set_relief(Gtk::RELIEF_NONE);
  del_btn->signal_clicked().connect([this, row] { remove_param_row(row); });

  row->set_margin_start(4);
  row->set_margin_end(4);
  row->pack_start(*key_e,   false, false);
  row->pack_start(*eq_lbl,  false, false);
  row->pack_start(*val_e,   true,  true);
  row->pack_start(*del_btn, false, false);

  param_rows_.push_back({key_e, val_e, row});
  params_box_.pack_start(*row, false, false, 2);
  row->show_all();  // needed for rows added after show_all_children()
  update_preview();
}

void EditQueryDialog::remove_param_row(Gtk::Box* row_widget)
{
  auto it = std::find_if(param_rows_.begin(), param_rows_.end(),
      [row_widget](const ParamRow& r) { return r.widget == row_widget; });
  if (it == param_rows_.end())
    return;
  param_rows_.erase(it);
  params_box_.remove(*row_widget);
  update_preview();
}

// ---------------------------------------------------------------------------
// URL reconstruction
// ---------------------------------------------------------------------------

std::string EditQueryDialog::build_request() const
{
  std::string result = path_entry_.get_text().raw();
  bool first = true;
  for (const auto& r : param_rows_)
  {
    const std::string key = r.key_entry->get_text().raw();
    if (key.empty())
      continue;
    result += first ? '?' : '&';
    first = false;
    result += urlencode(key);
    const std::string val = r.val_entry->get_text().raw();
    if (!val.empty())
    {
      result += '=';
      result += urlencode(val);
    }
  }
  return result;
}

void EditQueryDialog::update_preview()
{
  preview_entry_.set_text(build_request());
}

std::string EditQueryDialog::get_result_request() const
{
  return build_request();
}
