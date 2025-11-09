#include "CompareCore.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>

namespace CoinVisualTests {

bool load_image(const std::string& path, Image& out, int desired_channels) {
  if (desired_channels != 4) {
    return false;
  }
  int channels = 0;
  stbi_set_flip_vertically_on_load(false);
  stbi_uc* data = stbi_load(path.c_str(), &out.width, &out.height, &channels, desired_channels);
  if (!data) {
    return false;
  }
  const size_t total = static_cast<size_t>(out.width) * out.height * 4;
  out.pixels.assign(data, data + total);
  stbi_image_free(data);
  return true;
}

bool write_image(const std::string& path, const Image& image) {
  stbi_flip_vertically_on_write(false);
  return stbi_write_png(path.c_str(), image.width, image.height, 4,
                        image.pixels.data(), image.width * 4) != 0;
}

DiffResult compare_images(const Image& expected,
                          const Image& actual,
                          const CoinVisualTests::ToleranceSpec& tolerance) {
  DiffResult result;
  result.metrics.width = expected.width;
  result.metrics.height = expected.height;
  if (expected.width != actual.width || expected.height != actual.height) {
    result.metrics.dimensions_match = false;
    return result;
  }

  const int width = expected.width;
  const int height = expected.height;
  const size_t total_pixels = static_cast<size_t>(width) * height;
  result.metrics.total_pixels = static_cast<int>(total_pixels);
  result.diff_pixels.assign(total_pixels * 4, 0);
  double sum_sq = 0.0;
  size_t differing_pixels = 0;
  int max_abs_diff = 0;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t idx = static_cast<size_t>(y) * width + x;
      const size_t base = idx * 4;
      int local_max = 0;
      bool pixel_diff = false;
      for (int channel = 0; channel < 4; ++channel) {
        const int difference = std::abs(static_cast<int>(expected.pixels[base + channel]) -
                                        static_cast<int>(actual.pixels[base + channel]));
        sum_sq += static_cast<double>(difference) * difference;
        local_max = std::max(local_max, difference);
        pixel_diff = pixel_diff || difference > tolerance.per_channel;
        max_abs_diff = std::max(max_abs_diff, difference);
      }
      if (pixel_diff) {
        ++differing_pixels;
      }
      const int intensity = std::min(255, local_max * 4);
      result.diff_pixels[base + 0] = static_cast<uint8_t>(intensity);
      result.diff_pixels[base + 1] = static_cast<uint8_t>(intensity);
      result.diff_pixels[base + 2] = static_cast<uint8_t>(intensity);
      result.diff_pixels[base + 3] = 255;
    }
  }

  result.metrics.compared_pixels = static_cast<int>(total_pixels);
  result.metrics.differing_pixels = static_cast<int>(differing_pixels);
  result.metrics.max_abs_diff = max_abs_diff;
  if (total_pixels == 0) {
    result.metrics.pass = false;
    return result;
  }
  result.metrics.diff_pct = 100.0 * static_cast<double>(differing_pixels) /
                            static_cast<double>(total_pixels);
  result.metrics.rmse = std::sqrt(sum_sq / (static_cast<double>(total_pixels) * 4.0));
  result.metrics.pass = result.metrics.diff_pct <= tolerance.max_diff_pct &&
                        (tolerance.rmse <= 0.0 || result.metrics.rmse <= tolerance.rmse);
  return result;
}

bool write_metrics_json(const std::string& path, const DiffMetrics& metrics) {
  std::ofstream out(path);
  if (!out) {
    return false;
  }
  out << "{\n";
  out << "  \"width\": " << metrics.width << ",\n";
  out << "  \"height\": " << metrics.height << ",\n";
  out << "  \"dimensions_match\": "
      << (metrics.dimensions_match ? "true" : "false") << ",\n";
  out << "  \"total_pixels\": " << metrics.total_pixels << ",\n";
  out << "  \"compared_pixels\": " << metrics.compared_pixels << ",\n";
  out << "  \"differing_pixels\": " << metrics.differing_pixels << ",\n";
  out << std::fixed << std::setprecision(6);
  out << "  \"diff_pct\": " << metrics.diff_pct << ",\n";
  out << "  \"max_abs_diff\": " << metrics.max_abs_diff << ",\n";
  out << "  \"rmse\": " << metrics.rmse << ",\n";
  out << "  \"pass\": " << (metrics.pass ? "true" : "false") << "\n";
  out << "}\n";
  return true;
}

} // namespace CoinVisualTests
