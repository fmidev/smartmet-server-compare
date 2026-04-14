#pragma once
#include "ResultViewer.h"

#include <gdkmm/pixbuf.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/scrolledwindow.h>

#include <string>

/**
 * ResultViewer for image content (PNG, JPEG, SVG, PDF).
 *
 * Display modes selected via radio buttons:
 *   - Animate   – toggles between image 1 and image 2 once per second
 *   - Image 1   – shows the first server's image
 *   - Image 2   – shows the second server's image
 *   - Difference – contrast-stretched per-pixel difference (via Magick++)
 *
 * The PSNR value computed by CompareRunner is shown in a label.
 */
class ImageDiffViewer : public ResultViewer
{
 public:
  ImageDiffViewer();
  ~ImageDiffViewer() override;

  const char* name() const override { return "image-diff"; }
  bool can_handle(const CompareResult& result) const override;

  void show(const CompareResult& result,
            const std::string& label1,
            const std::string& label2) override;

  void clear() override;
  Gtk::Widget& widget() override { return outer_box_; }

 private:
  enum class Mode { ANIMATE, IMAGE1, IMAGE2, DIFFERENCE };

  void set_mode(Mode m);
  void show_pixbuf(const Glib::RefPtr<Gdk::Pixbuf>& pb);
  bool on_animation_tick();
  void on_export_clicked();

  static Glib::RefPtr<Gdk::Pixbuf> pixbuf_from_blob(const std::string& blob);

  // Widgets
  Gtk::Box outer_box_{Gtk::ORIENTATION_VERTICAL, 4};
  Gtk::Box toolbar_{Gtk::ORIENTATION_HORIZONTAL, 8};

  Gtk::RadioButton rb_animate_{"Animate"};
  Gtk::RadioButton rb_img1_{"Image 1"};
  Gtk::RadioButton rb_img2_{"Image 2"};
  Gtk::RadioButton rb_diff_{"Difference"};
  Gtk::Button btn_export_{"Export animation…"};

  Gtk::Label info_label_;

  Gtk::ScrolledWindow scroll_;
  Gtk::Image image_;

  // State
  Mode mode_{Mode::ANIMATE};
  sigc::connection timer_;
  bool animation_showing_first_{true};

  // Cached pixbufs for the current result
  Glib::RefPtr<Gdk::Pixbuf> pb1_;
  Glib::RefPtr<Gdk::Pixbuf> pb2_;
  Glib::RefPtr<Gdk::Pixbuf> pb_diff_;

  // Keep raw blobs for lazy diff computation
  std::string blob1_;
  std::string blob2_;
};
