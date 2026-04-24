#pragma once
#include "HttpClient.h"

#include <glibmm/dispatcher.h>
#include <gtkmm/box.h>
#include <gtkmm/dialog.h>
#include <gtkmm/label.h>
#include <gtkmm/notebook.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

/**
 * Modal dialog that re-sends one request against each configured server
 * and displays both sides in a "curl -v" style transcript — request
 * headers (`> …`), response status + headers (`< …`), and response body.
 *
 * The HTTP work runs on a worker thread so the GTK main loop stays alive.
 * The dialog takes ownership of its worker and joins it in the destructor.
 */
class InspectDialog : public Gtk::Dialog
{
 public:
  InspectDialog(Gtk::Window& parent,
                std::string request_string,
                std::string server1_url,
                std::string server2_url);

  ~InspectDialog() override;

 private:
  void start_request();
  void on_done();
  void render_transcript(Gtk::TextView& view,
                         const std::string& full_url,
                         const HttpClient::Response& resp);

  std::string request_;
  std::string server1_url_;
  std::string server2_url_;

  Gtk::Notebook notebook_;
  Gtk::ScrolledWindow scroll1_;
  Gtk::ScrolledWindow scroll2_;
  Gtk::TextView view1_;
  Gtk::TextView view2_;
  Gtk::Label status_label_;

  // Thread plumbing
  std::thread                       worker_;
  std::shared_ptr<std::atomic<bool>> cancel_;
  Glib::Dispatcher                  done_dispatcher_;
  std::mutex                        mutex_;
  HttpClient::Response              resp1_;
  HttpClient::Response              resp2_;
  bool                              finished_{false};
};
