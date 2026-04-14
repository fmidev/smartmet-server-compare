#pragma once
#include <string>

/**
 * Image comparison utilities using ImageMagick (Magick++).
 *
 * compute_psnr()
 *   Loads both blobs via Magick++, computes MSE, and returns the PSNR in
 *   decibels.  Returns +inf for identical images.  Throws on load failure or
 *   dimension mismatch.
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
