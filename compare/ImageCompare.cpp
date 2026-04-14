#include "ImageCompare.h"

#include <Magick++.h>

#include <cmath>
#include <limits>
#include <stdexcept>

// ---------------------------------------------------------------------------
// One-time Magick++ initialisation (thread-safe after C++11)
// ---------------------------------------------------------------------------

static void ensure_magick_init()
{
  static bool once = [] {
    Magick::InitializeMagick(nullptr);
    return true;
  }();
  (void)once;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

double compute_psnr(const std::string& blob1, const std::string& blob2)
{
  ensure_magick_init();

  Magick::Blob b1(blob1.data(), blob1.size());
  Magick::Blob b2(blob2.data(), blob2.size());

  Magick::Image img1(b1);
  Magick::Image img2(b2);

  if (img1.size() != img2.size())
    throw std::runtime_error("Images have different dimensions: " +
                             std::to_string(img1.size().width()) + "x" +
                             std::to_string(img1.size().height()) + " vs " +
                             std::to_string(img2.size().width()) + "x" +
                             std::to_string(img2.size().height()));

  const double mse =
      img1.compare(img2, Magick::MetricType::MeanSquaredErrorMetric);

  if (mse == 0)
    return std::numeric_limits<double>::infinity();

  return 20.0 * std::log10(1.0 / std::sqrt(mse));
}

std::string make_diff_image_png(const std::string& blob1, const std::string& blob2)
{
  ensure_magick_init();

  Magick::Blob b1(blob1.data(), blob1.size());
  Magick::Blob b2(blob2.data(), blob2.size());

  Magick::Image img1(b1);
  Magick::Image img2(b2);

  // Resize img2 to match img1 if dimensions differ (nearest-neighbour so it
  // doesn't mask real differences with interpolation artefacts).
  if (img1.size() != img2.size())
    img2.resize(img1.size());

  Magick::Image diff = img1;
  diff.composite(img2, 0, 0, Magick::CompositeOperator::DifferenceCompositeOp);
  diff.contrastStretch(0.0, 1.0);

  Magick::Blob out;
  diff.magick("PNG");
  diff.write(&out);

  return std::string(static_cast<const char*>(out.data()), out.length());
}

std::string rasterize_to_png(const std::string& blob)
{
  ensure_magick_init();

  Magick::Blob b(blob.data(), blob.size());
  Magick::Image img(b);

  Magick::Blob out;
  img.magick("PNG");
  img.write(&out);

  return std::string(static_cast<const char*>(out.data()), out.length());
}

void export_animation(const std::string& blob1,
                      const std::string& blob2,
                      const std::string& path,
                      int delay_cs)
{
  ensure_magick_init();

  Magick::Blob b1(blob1.data(), blob1.size());
  Magick::Blob b2(blob2.data(), blob2.size());

  Magick::Image img1(b1);
  Magick::Image img2(b2);

  // Match dimensions so the animation frames are consistent.
  if (img1.size() != img2.size())
    img2.resize(img1.size());

  img1.animationDelay(delay_cs);
  img2.animationDelay(delay_cs);
  img1.animationIterations(0);  // loop forever

  std::vector<Magick::Image> frames{img1, img2};
  Magick::writeImages(frames.begin(), frames.end(), path);
}
