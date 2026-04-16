#include "ImageDiffViewer.h"
#include "ContentHandler.h"
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

  text_view_.set_editable(false);
  text_view_.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
  text_view_.set_monospace(true);

  content_stack_.add(image_, "image");
  content_stack_.add(text_view_, "text");
  content_stack_.set_transition_type(Gtk::STACK_TRANSITION_TYPE_NONE);

  scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  scroll_.add(content_stack_);

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
  // Handle when at least one side is an image
  return is_image_kind(result.kind1) || is_image_kind(result.kind2);
}

void ImageDiffViewer::show(const CompareResult& result,
                           const std::string& label1,
                           const std::string& label2)
{
  blob1_ = result.body1;
  blob2_ = result.body2;
  text1_.clear();
  text2_.clear();

  const bool img1 = is_image_kind(result.kind1);
  const bool img2 = is_image_kind(result.kind2);

  // Load pixbufs only for image sides
  pb1_ = img1 ? pixbuf_from_blob(blob1_) : Glib::RefPtr<Gdk::Pixbuf>();
  pb2_ = img2 ? pixbuf_from_blob(blob2_) : Glib::RefPtr<Gdk::Pixbuf>();
  pb_diff_.reset();

  // For mixed content, store the formatted text for the text side
  if (!img1)
    text1_ = result.formatted1.empty() ? result.body1 : result.formatted1;
  if (!img2)
    text2_ = result.formatted2.empty() ? result.body2 : result.formatted2;

  // Info label
  std::ostringstream oss;
  if (is_mixed())
  {
    oss << "Content mismatch: "
        << label1 << " → " << content_kind_label(result.kind1) << "    "
        << label2 << " → " << content_kind_label(result.kind2);
  }
  else
  {
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
  }
  info_label_.set_text(oss.str());

  // Disable modes that don't apply to mixed content
  const bool mixed = is_mixed();
  rb_animate_.set_sensitive(!mixed);
  rb_diff_.set_sensitive(!mixed);
  btn_export_.set_sensitive(!mixed);

  // Default to Image 1 for mixed, Animate for normal
  if (mixed)
  {
    rb_img1_.set_active(true);
    set_mode(Mode::IMAGE1);
  }
  else
  {
    rb_animate_.set_active(true);
    set_mode(Mode::ANIMATE);
  }
}

void ImageDiffViewer::clear()
{
  timer_.disconnect();
  pb1_.reset();
  pb2_.reset();
  pb_diff_.reset();
  blob1_.clear();
  blob2_.clear();
  text1_.clear();
  text2_.clear();
  image_.clear();
  text_view_.get_buffer()->set_text("");
  info_label_.set_text("");
}

// ---------------------------------------------------------------------------
// Mode switching
// ---------------------------------------------------------------------------

bool ImageDiffViewer::is_mixed() const
{
  // Mixed = one side has a pixbuf (image) and the other has text
  return (!text1_.empty() || !text2_.empty());
}

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
      if (!text1_.empty())
        show_text(text1_);
      else
        show_pixbuf(pb1_);
      break;

    case Mode::IMAGE2:
      if (!text2_.empty())
        show_text(text2_);
      else
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
  content_stack_.set_visible_child("image");
}

void ImageDiffViewer::show_text(const std::string& text)
{
  text_view_.get_buffer()->set_text(text);
  content_stack_.set_visible_child("text");
}

bool ImageDiffViewer::on_animation_tick()
{
  animation_showing_first_ = !animation_showing_first_;
  show_pixbuf(animation_showing_first_ ? pb1_ : pb2_);
  return true;
}

// ---------------------------------------------------------------------------
// Export animation
// ---------------------------------------------------------------------------

void ImageDiffViewer::on_export_clicked()
{
  if (blob1_.empty() || blob2_.empty())
    return;

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
