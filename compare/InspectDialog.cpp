#include "InspectDialog.h"

#include <gtkmm/buttonbox.h>

#include <chrono>
#include <sstream>
#include <utility>

namespace
{

// Prepend every line of `text` with `prefix`.  Keeps the existing line
// terminators; a trailing line without terminator gets the prefix too so
// the whole block reads uniformly.
std::string prefix_lines(const std::string& text, const char* prefix)
{
  if (text.empty())
    return {};
  std::ostringstream out;
  std::size_t pos = 0;
  while (pos < text.size())
  {
    auto nl = text.find('\n', pos);
    if (nl == std::string::npos)
    {
      out << prefix << text.substr(pos);
      break;
    }
    out << prefix << text.substr(pos, nl - pos + 1);
    pos = nl + 1;
  }
  return out.str();
}

// Strip a trailing newline so the caller can cleanly append a blank line
// with its own prefix.
std::string rstrip_newline(std::string s)
{
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
    s.pop_back();
  return s;
}

}  // namespace

InspectDialog::InspectDialog(Gtk::Window& parent,
                             std::string request_string,
                             std::string server1_url,
                             std::string server2_url)
    : Gtk::Dialog("Request inspector", parent, true),
      request_(std::move(request_string)),
      server1_url_(std::move(server1_url)),
      server2_url_(std::move(server2_url))
{
  set_default_size(900, 700);
  set_modal(true);

  view1_.set_editable(false);
  view1_.set_monospace(true);
  view1_.set_cursor_visible(false);
  view1_.set_wrap_mode(Gtk::WRAP_NONE);

  view2_.set_editable(false);
  view2_.set_monospace(true);
  view2_.set_cursor_visible(false);
  view2_.set_wrap_mode(Gtk::WRAP_NONE);

  scroll1_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  scroll2_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  scroll1_.add(view1_);
  scroll2_.add(view2_);

  notebook_.append_page(scroll1_, "Server 1");
  notebook_.append_page(scroll2_, "Server 2");

  auto* box = get_content_area();
  status_label_.set_xalign(0.0f);
  status_label_.set_margin_start(8);
  status_label_.set_margin_top(4);
  status_label_.set_margin_bottom(4);
  status_label_.set_text("Sending request to both servers…");
  box->pack_start(status_label_, false, false);
  box->pack_start(notebook_, true, true);

  add_button("_Close", Gtk::RESPONSE_CLOSE);

  done_dispatcher_.connect(sigc::mem_fun(*this, &InspectDialog::on_done));

  show_all_children();

  start_request();
}

InspectDialog::~InspectDialog()
{
  // Cancel and join before members are destroyed — the worker holds a
  // pointer into `done_dispatcher_` and the response-body strings.
  if (cancel_)
    cancel_->store(true);
  if (worker_.joinable())
    worker_.join();
}

void InspectDialog::start_request()
{
  cancel_ = std::make_shared<std::atomic<bool>>(false);
  auto cancel = cancel_;

  auto url1 = server1_url_ + request_;
  auto url2 = server2_url_ + request_;

  worker_ = std::thread(
      [this, cancel, url1 = std::move(url1), url2 = std::move(url2)]()
      {
        HttpClient client(60);
        client.add("s1", url1);
        client.add("s2", url2);

        // Watcher thread polls `cancel` and propagates to HttpClient::stop()
        // so the dialog can be closed while long requests are in flight.
        std::atomic<bool> done_flag{false};
        std::thread cancel_watcher([&client, cancel, &done_flag]() {
          while (!cancel->load() && !done_flag.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
          if (cancel->load())
            client.stop();
        });

        client.execute();
        done_flag.store(true);
        cancel_watcher.join();

        {
          std::lock_guard<std::mutex> lock(mutex_);
          resp1_    = client.response("s1");
          resp2_    = client.response("s2");
          finished_ = true;
        }
        done_dispatcher_.emit();
      });
}

void InspectDialog::on_done()
{
  HttpClient::Response r1;
  HttpClient::Response r2;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!finished_)
      return;
    r1 = std::move(resp1_);
    r2 = std::move(resp2_);
  }

  render_transcript(view1_, server1_url_ + request_, r1);
  render_transcript(view2_, server2_url_ + request_, r2);

  auto fmt_status = [](const HttpClient::Response& r) -> std::string {
    if (!r.error.empty())
      return "ERROR: " + r.error;
    return "HTTP " + std::to_string(r.status_code);
  };
  status_label_.set_text("Server 1: " + fmt_status(r1) +
                         "    Server 2: " + fmt_status(r2));
}

void InspectDialog::render_transcript(Gtk::TextView& view,
                                       const std::string& full_url,
                                       const HttpClient::Response& resp)
{
  std::ostringstream out;
  out << "# URL\n" << full_url << "\n\n";

  if (!resp.request_headers.empty())
    out << prefix_lines(rstrip_newline(resp.request_headers) + "\n", "> ") << "\n";
  else
    out << "> (request headers unavailable)\n\n";

  if (!resp.response_headers.empty())
    out << prefix_lines(rstrip_newline(resp.response_headers) + "\n", "< ") << "\n";
  else if (!resp.error.empty())
    out << "< (no response — transport error)\n\n";

  if (!resp.error.empty())
    out << "* Transport error: " << resp.error << "\n\n";

  out << "# Body (" << resp.body.size() << " bytes)\n";
  out << resp.body;
  if (!resp.body.empty() && resp.body.back() != '\n')
    out << '\n';

  view.get_buffer()->set_text(out.str());
}
