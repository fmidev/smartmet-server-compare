#pragma once
#include <cstddef>
#include <string>

/**
 * Image comparison utilities using ImageMagick (Magick++).
 *
 * compute_psnr()
 *   Loads both blobs via Magick++, computes MSE, and returns the PSNR in
 *   decibels.  Returns +inf for identical images.  Throws on load failure or
 *   dimension mismatch.
 *
 * structural_image_diff()
 *   Anti-aliasing-aware structural comparison.  A global metric like PSNR
 *   cannot tell "sub-pixel anti-aliasing and font-hinting jitter spread thinly
 *   over every text/symbol edge" (harmless — the two servers just rendered the
 *   same glyphs slightly differently) from "a spatially clustered solid change
 *   such as a dropped map feature" (a real difference).  Both can produce the
 *   same low PSNR.  This ports the algorithm from smartmet-library-regression
 *   (without depending on it): it compares per pixel with the pixelmatch YIQ
 *   perceptual metric, discards anti-aliasing-class pixels, keeps only
 *   high-amplitude differences, clusters them with 8-connectivity, and judges
 *   on cluster geometry.
 *
 * make_diff_image_png()
 *   Computes a per-pixel "Difference" composite of the two images, applies a
 *   contrast stretch so small differences become visible, and returns the
 *   result encoded as a PNG blob.  Throws on failure.
 *
 * Both functions accept raw image bytes (the HTTP response body) – any
 * format that ImageMagick can decode (PNG, JPEG, GIF, SVG, PDF, …).
 */

double compute_psnr(const std::string& blob1, const std::string& blob2);

// Tunables for structural_image_diff().  Defaults match the WMS-tuned values
// used by smartmet-library-regression.
struct StructuralDiffOptions
{
  double threshold = 0.1;      // base per-pixel YIQ sensitivity (pixelmatch)
  double strong = 0.25;        // normalized amplitude for a "significant" pixel
  long minClusterArea = 8;     // clusters smaller than this are counted as noise
  long failClusterArea = 80;   // any surviving cluster >= this -> real difference

  // Anti-aliasing flood guard: a thin line that moved ~1px along its whole
  // length is misread pixel-by-pixel as anti-aliasing, never forming a
  // significant cluster, yet it floods the AA-ignored count far beyond a
  // genuine per-edge fringe.  Flag a difference when the AA-classified pixels
  // exceed BOTH an absolute floor and a fraction of the frame.  Requiring both
  // avoids false positives on small blurred images and huge sparse ones.  Set
  // maxAaFraction <= 0 to disable.
  double maxAaFraction = 0.003;  // 0.3% of the image area
  long aaFloodMinPixels = 1000;  // absolute floor below which the fraction is not trusted
};

// Outcome of structural_image_diff().  `computed` is false when the comparison
// was not run (e.g. byte-identical bodies short-circuited earlier).
struct ImageDiffResult
{
  bool computed = false;
  bool size_mismatch = false;      // dimensions differ -> always a difference
  std::size_t width = 0;
  std::size_t height = 0;
  long candidate_pixels = 0;       // pixels exceeding the base threshold
  long aa_ignored_pixels = 0;      // candidates discarded as anti-aliasing / text jitter
  long significant_pixels = 0;     // pixels in clusters >= minClusterArea
  long cluster_count = 0;          // number of such clusters
  long largest_cluster_area = 0;   // area of the biggest cluster
  bool aa_flood = false;           // AA-ignored pixels flooded the frame
  bool differs = false;            // verdict: true = a real (non-jitter) difference
};

// Compare two raw image blobs structurally.  Throws only on decode failure;
// a dimension mismatch is reported via ImageDiffResult::size_mismatch.
ImageDiffResult structural_image_diff(const std::string& blob1,
                                      const std::string& blob2,
                                      const StructuralDiffOptions& opts = {});

std::string make_diff_image_png(const std::string& blob1, const std::string& blob2);

// Rasterise any image format Magick++ can decode to PNG bytes.  Useful as a
// fallback when GdkPixbuf cannot handle the format directly (e.g. PDF).
std::string rasterize_to_png(const std::string& blob);

// Write a two-frame looping animation to `path`.  The output format is
// determined by the file extension (.webp, .gif, .apng, …).  `delay_cs` is
// the per-frame delay in centiseconds (100 = 1 second).  Throws on failure.
void export_animation(const std::string& blob1,
                      const std::string& blob2,
                      const std::string& path,
                      int delay_cs = 100);
