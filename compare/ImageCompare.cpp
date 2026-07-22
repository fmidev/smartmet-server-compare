#include "ImageCompare.h"

#include <Magick++.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <stdexcept>
#include <vector>

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

// ---------------------------------------------------------------------------
// Structural, anti-aliasing-aware comparison.
//
// Ported from smartmet-library-regression (StructuralImageDiff) so this tool
// carries no dependency on that library.  The pixel metric is a port of
// mapbox/pixelmatch (YIQ perceptual color delta), the anti-aliasing detector
// is Vyšniauskas', and the verdict is based on 8-connected cluster geometry.
// ---------------------------------------------------------------------------

namespace
{
inline double rgb2y(double r, double g, double b)
{
  return r * 0.29889531 + g * 0.58662247 + b * 0.11448223;
}
inline double rgb2i(double r, double g, double b)
{
  return r * 0.59597799 - g * 0.27417610 - b * 0.32180189;
}
inline double rgb2q(double r, double g, double b)
{
  return r * 0.21147017 - g * 0.52261711 + b * 0.31114694;
}
inline double blendWhite(double c, double a)
{
  return 255.0 + (c - 255.0) * a;
}

// Signed squared YIQ color delta.  With onlyY == true, returns the signed
// brightness difference (used by the anti-aliasing detector).
double colorDelta(const uint8_t* a, const uint8_t* b, std::size_t i, std::size_t j,
                  bool onlyY = false)
{
  double r1 = a[i], g1 = a[i + 1], b1 = a[i + 2], a1 = a[i + 3];
  double r2 = b[j], g2 = b[j + 1], b2 = b[j + 2], a2 = b[j + 3];
  if (a1 == a2 && r1 == r2 && g1 == g2 && b1 == b2)
    return 0;
  if (a1 < 255)
  {
    a1 /= 255;
    r1 = blendWhite(r1, a1);
    g1 = blendWhite(g1, a1);
    b1 = blendWhite(b1, a1);
  }
  if (a2 < 255)
  {
    a2 /= 255;
    r2 = blendWhite(r2, a2);
    g2 = blendWhite(g2, a2);
    b2 = blendWhite(b2, a2);
  }
  const double y1 = rgb2y(r1, g1, b1), y2 = rgb2y(r2, g2, b2);
  const double y = y1 - y2;
  if (onlyY)
    return y;
  const double di = rgb2i(r1, g1, b1) - rgb2i(r2, g2, b2);
  const double dq = rgb2q(r1, g1, b1) - rgb2q(r2, g2, b2);
  const double delta = 0.5053 * y * y + 0.299 * di * di + 0.1957 * dq * dq;
  return (y1 > y2) ? -delta : delta;
}

bool hasManySiblings(const uint8_t* img, int x1, int y1, int w, int h)
{
  const int x0 = std::max(x1 - 1, 0), y0 = std::max(y1 - 1, 0);
  const int x2 = std::min(x1 + 1, w - 1), y2 = std::min(y1 + 1, h - 1);
  const std::size_t pos = (std::size_t(y1) * w + x1) * 4;
  int zeroes = (x1 == x0 || x1 == x2 || y1 == y0 || y1 == y2) ? 1 : 0;
  for (int x = x0; x <= x2; ++x)
    for (int y = y0; y <= y2; ++y)
    {
      if (x == x1 && y == y1)
        continue;
      const std::size_t p2 = (std::size_t(y) * w + x) * 4;
      if (img[pos] == img[p2] && img[pos + 1] == img[p2 + 1] && img[pos + 2] == img[p2 + 2] &&
          img[pos + 3] == img[p2 + 3])
      {
        if (++zeroes > 2)
          return true;
      }
    }
  return false;
}

// True if pixel (x1,y1) looks like anti-aliasing in `img`, cross-checked
// against `other`.
bool antialiased(const uint8_t* img, int x1, int y1, int w, int h, const uint8_t* other)
{
  const int x0 = std::max(x1 - 1, 0), y0 = std::max(y1 - 1, 0);
  const int x2 = std::min(x1 + 1, w - 1), y2 = std::min(y1 + 1, h - 1);
  const std::size_t pos = (std::size_t(y1) * w + x1) * 4;
  int zeroes = (x1 == x0 || x1 == x2 || y1 == y0 || y1 == y2) ? 1 : 0;
  double mn = 0, mx = 0;
  int minX = -1, minY = -1, maxX = -1, maxY = -1;
  for (int x = x0; x <= x2; ++x)
    for (int y = y0; y <= y2; ++y)
    {
      if (x == x1 && y == y1)
        continue;
      const double delta = colorDelta(img, img, pos, (std::size_t(y) * w + x) * 4, true);
      if (delta == 0)
      {
        if (++zeroes > 2)
          return false;
      }
      else if (delta < mn)
      {
        mn = delta;
        minX = x;
        minY = y;
      }
      else if (delta > mx)
      {
        mx = delta;
        maxX = x;
        maxY = y;
      }
    }
  if (mn == 0 || mx == 0)
    return false;
  return (hasManySiblings(img, minX, minY, w, h) && hasManySiblings(other, minX, minY, w, h)) ||
         (hasManySiblings(img, maxX, maxY, w, h) && hasManySiblings(other, maxX, maxY, w, h));
}

struct Cluster
{
  int minx, miny, maxx, maxy;
  long area;
};

}  // namespace

ImageDiffResult structural_image_diff(const std::string& blob1,
                                      const std::string& blob2,
                                      const StructuralDiffOptions& opts)
{
  ensure_magick_init();

  ImageDiffResult res;

  Magick::Blob mb1(blob1.data(), blob1.size());
  Magick::Blob mb2(blob2.data(), blob2.size());
  Magick::Image im1(mb1);
  Magick::Image im2(mb2);

  const std::size_t w = im1.columns(), h = im1.rows();
  res.computed = true;
  res.width = w;
  res.height = h;
  if (w != im2.columns() || h != im2.rows())
  {
    res.size_mismatch = true;
    res.differs = true;
    return res;
  }
  if (w == 0 || h == 0)
    return res;

  std::vector<uint8_t> a(w * h * 4), b(w * h * 4);
  im1.write(0, 0, w, h, "RGBA", Magick::CharPixel, a.data());
  im2.write(0, 0, w, h, "RGBA", Magick::CharPixel, b.data());

  const double baseSq = opts.threshold * opts.threshold;

  // mask: 0 = same, 1 = weak/AA difference, 2 = significant difference
  std::vector<uint8_t> mask(w * h, 0);
  for (int y = 0; y < (int)h; ++y)
    for (int x = 0; x < (int)w; ++x)
    {
      const std::size_t p = (std::size_t(y) * w + x) * 4;
      const double nd = std::fabs(colorDelta(a.data(), b.data(), p, p, false)) / 35215.0;
      if (nd <= baseSq)
        continue;
      ++res.candidate_pixels;
      const bool isStrong = std::sqrt(nd) >= opts.strong;
      const bool isAA = antialiased(a.data(), x, y, w, h, b.data()) ||
                        antialiased(b.data(), x, y, w, h, a.data());
      if (isAA)
        ++res.aa_ignored_pixels;
      mask[std::size_t(y) * w + x] = (isStrong && !isAA) ? 2 : 1;
    }

  // 8-connected clustering of the significant pixels.
  std::vector<int> label(w * h, 0);
  std::vector<Cluster> clusters;
  for (int y = 0; y < (int)h; ++y)
    for (int x = 0; x < (int)w; ++x)
    {
      const std::size_t idx = std::size_t(y) * w + x;
      if (mask[idx] != 2 || label[idx])
        continue;
      Cluster c{x, y, x, y, 0};
      std::queue<std::pair<int, int>> q;
      q.push({x, y});
      label[idx] = (int)clusters.size() + 1;
      while (!q.empty())
      {
        const auto [cx, cy] = q.front();
        q.pop();
        ++c.area;
        c.minx = std::min(c.minx, cx);
        c.maxx = std::max(c.maxx, cx);
        c.miny = std::min(c.miny, cy);
        c.maxy = std::max(c.maxy, cy);
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx)
          {
            const int nx = cx + dx, ny = cy + dy;
            if (nx < 0 || ny < 0 || nx >= (int)w || ny >= (int)h)
              continue;
            const std::size_t ni = std::size_t(ny) * w + nx;
            if (mask[ni] == 2 && !label[ni])
            {
              label[ni] = (int)clusters.size() + 1;
              q.push({nx, ny});
            }
          }
      }
      clusters.push_back(c);
    }

  for (const auto& c : clusters)
  {
    if (c.area < opts.minClusterArea)
      continue;
    ++res.cluster_count;
    res.significant_pixels += c.area;
    res.largest_cluster_area = std::max(res.largest_cluster_area, c.area);
    if (c.area >= opts.failClusterArea)
      res.differs = true;
  }

  // Anti-aliasing flood guard (see StructuralDiffOptions::maxAaFraction).
  if (opts.maxAaFraction > 0 && res.aa_ignored_pixels >= opts.aaFloodMinPixels &&
      res.aa_ignored_pixels >= (long)(opts.maxAaFraction * double(w) * double(h)))
  {
    res.aa_flood = true;
    res.differs = true;
  }

  return res;
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
