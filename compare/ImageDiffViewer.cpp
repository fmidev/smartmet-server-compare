#include "ImageDiffViewer.h"
#include "ImageCompare.h"

#include <giomm/memoryinputstream.h>
#include <glibmm/bytes.h>
#include <glibmm/main.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/messagedialog.h>

#include <cmath>
#include <iomanip>
#include <sstream>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ImageDiffViewer::ImageDiffViewer()
{
  // Group the radio buttons
  rb_img1_.join_group(rb_animate_);
  rb_img2_.join_group(rb_animate_);
  rb_diff_.join_group(rb_animate_);

  rb_animate_.signal_toggled().connect([this] { if (rb_animate_.get_active()) set_mode(Mode::ANIMATE); });
  rb_img1_.signal_toggled().connect([this]    { if (rb_img1_.get_active())    set_mode(Mode::IMAGE1); });
  rb_img2_.signal_toggled().connect([this]    { if (rb_img2_.get_active())    set_mode(Mode::IMAGE2); });
  rb_diff_.signal_toggled().connect([this]    { if (rb_diff_.get_active())    set_mode(Mode::DIFFERENCE); });

  btn_export_.signal_clicked().connect(
      sigc::mem_fun(*this, &ImageDiffViewer::on_export_clicked));

  toolbar_.set_border_width(4);
  toolbar_.pack_start(rb_animate_, false, false);
  toolbar_.pack_start(rb_img1_, false, false);
  toolbar_.pack_start(rb_img2_, false, false);
  toolbar_.pack_start(rb_diff_, false, false);
  toolbar_.pack_start(btn_export_, false, false, 8);
  toolbar_.pack_start(info_label_, false, false, 16);

  scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  scroll_.add(image_);

  outer_box_.pack_start(toolbar_, false, false);
  outer_box_.pack_start(scroll_, true, true);
  outer_box_.show_all();
}

ImageDiffViewer::~ImageDiffViewer()
{
  timer_.disconnect();
}

// ---------------------------------------------------------------------------
// ResultViewer interface
// ---------------------------------------------------------------------------

bool ImageDiffViewer::can_handle(const CompareResult& result) const
{
  return is_image_kind(result.kind1) && is_image_kind(result.kind2);
}

void ImageDiffViewer::show(const CompareResult& result,
                           const std::string& label1,
                           const std::string& label2)
{
  // Cache raw blobs for lazy diff computation
  blob1_ = result.body1;
  blob2_ = result.body2;

  // Load pixbufs
  pb1_ = pixbuf_from_blob(blob1_);
  pb2_ = pixbuf_from_blob(blob2_);
  pb_diff_.reset();  // computed lazily

  // Info label
  std::ostringstream oss;
  if (!std::isnan(result.psnr))
  {
    oss << "PSNR: ";
    if (std::isinf(result.psnr))
      oss << "∞ (identical)";
    else
      oss << std::fixed << std::setprecision(1) << result.psnr << " dB";
  }
  if (pb1_ && pb2_)
  {
    oss << "    " << label1 << ": " << pb1_->get_width() << "×" << pb1_->get_height()
        << "    " << label2 << ": " << pb2_->get_width() << "×" << pb2_->get_height();
  }
  info_label_.set_text(oss.str());

  // Start in Animate mode
  rb_animate_.set_active(true);
  set_mode(Mode::ANIMATE);
}

void ImageDiffViewer::clear()
{
  timer_.disconnect();
  pb1_.reset();
  pb2_.reset();
  pb_diff_.reset();
  blob1_.clear();
  blob2_.clear();
  image_.clear();
  info_label_.set_text("");
}

// ---------------------------------------------------------------------------
// Mode switching
// ---------------------------------------------------------------------------

void ImageDiffViewer::set_mode(Mode m)
{
  timer_.disconnect();
  mode_ = m;

  switch (m)
  {
    case Mode::ANIMATE:
      animation_showing_first_ = true;
      show_pixbuf(pb1_);
      timer_ = Glib::signal_timeout().connect(
          sigc::mem_fun(*this, &ImageDiffViewer::on_animation_tick), 1000);
      break;

    case Mode::IMAGE1:
      show_pixbuf(pb1_);
      break;

    case Mode::IMAGE2:
      show_pixbuf(pb2_);
      break;

    case Mode::DIFFERENCE:
      if (!pb_diff_ && !blob1_.empty() && !blob2_.empty())
      {
        try
        {
          std::string diff_png = make_diff_image_png(blob1_, blob2_);
          pb_diff_ = pixbuf_from_blob(diff_png);
        }
        catch (const std::exception& e)
        {
          info_label_.set_text(std::string("Diff error: ") + e.what());
        }
      }
      show_pixbuf(pb_diff_);
      break;
  }
}

void ImageDiffViewer::show_pixbuf(const Glib::RefPtr<Gdk::Pixbuf>& pb)
{
  if (pb)
    image_.set(pb);
  else
    image_.clear();
}

bool ImageDiffViewer::on_animation_tick()
{
  animation_showing_first_ = !animation_showing_first_;
  show_pixbuf(animation_showing_first_ ? pb1_ : pb2_);
  return true;  // keep the timer running
}

// ---------------------------------------------------------------------------
// Export animation
// ---------------------------------------------------------------------------

void ImageDiffViewer::on_export_clicked()
{
  if (blob1_.empty() || blob2_.empty())
    return;

  // Find the toplevel window for the dialog parent
  auto* toplevel = outer_box_.get_toplevel();
  auto* parent_win = dynamic_cast<Gtk::Window*>(toplevel);

  Gtk::FileChooserDialog dlg("Export animation", Gtk::FILE_CHOOSER_ACTION_SAVE);
  if (parent_win)
    dlg.set_transient_for(*parent_win);
  dlg.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dlg.add_button("_Save",   Gtk::RESPONSE_OK);
  dlg.set_do_overwrite_confirmation(true);
  dlg.set_current_name("comparison.webp");

  auto filter_webp = Gtk::FileFilter::create();
  filter_webp->set_name("WebP animation (*.webp)");
  filter_webp->add_pattern("*.webp");
  dlg.add_filter(filter_webp);

  auto filter_gif = Gtk::FileFilter::create();
  filter_gif->set_name("GIF animation (*.gif)");
  filter_gif->add_pattern("*.gif");
  dlg.add_filter(filter_gif);

  auto filter_all = Gtk::FileFilter::create();
  filter_all->set_name("All files");
  filter_all->add_pattern("*");
  dlg.add_filter(filter_all);

  if (dlg.run() != Gtk::RESPONSE_OK)
    return;

  const std::string path = dlg.get_filename();
  dlg.hide();

  try
  {
    export_animation(blob1_, blob2_, path);
    info_label_.set_text("Exported: " + path);
  }
  catch (const std::exception& e)
  {
    Gtk::MessageDialog err(std::string("Export failed: ") + e.what(),
                           false, Gtk::MESSAGE_ERROR);
    if (parent_win)
      err.set_transient_for(*parent_win);
    err.run();
  }
}

// ---------------------------------------------------------------------------
// Pixbuf loading from raw image bytes
// ---------------------------------------------------------------------------

Glib::RefPtr<Gdk::Pixbuf> ImageDiffViewer::pixbuf_from_blob(const std::string& blob)
{
  if (blob.empty())
    return {};

  try
  {
    auto stream = Gio::MemoryInputStream::create();
    stream->add_bytes(Glib::Bytes::create(blob.data(), blob.size()));
    return Gdk::Pixbuf::create_from_stream(stream);
  }
  catch (...)
  {
    // GdkPixbuf can't decode (e.g. PDF) — rasterize via Magick++ as fallback.
    try
    {
      std::string png = rasterize_to_png(blob);
      auto s = Gio::MemoryInputStream::create();
      s->add_bytes(Glib::Bytes::create(png.data(), png.size()));
      return Gdk::Pixbuf::create_from_stream(s);
    }
    catch (...)
    {
      return {};
    }
  }
}
