#include "CompareCommand.h"

#include <iostream>
#include <sstream>
#include <string>

#include "CompareCore.h"

namespace {

struct Config {
  std::string expected;
  std::string actual;
  std::string diff;
  std::string metrics;
  bool quiet = false;
  CoinVisualTests::ToleranceSpec tolerance;
};

void log_error(const Config& cfg, const std::string& message) {
  std::cerr << (cfg.quiet ? "  " : "") << message << '\n';
}

void print_usage() {
  std::cout << "Usage: CoinVisualTests compare --expected <png> --actual <png>"
            << " --diff <png> --metrics <json>"
            << " [--tolerance-per-channel N] [--max-diff-pct P] [--rmse R] [--quiet]\n";
}

bool parse_args(int argc, const char* argv[], Config& cfg) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--expected" && i + 1 < argc) {
      cfg.expected = argv[++i];
    } else if (arg == "--actual" && i + 1 < argc) {
      cfg.actual = argv[++i];
    } else if (arg == "--diff" && i + 1 < argc) {
      cfg.diff = argv[++i];
    } else if (arg == "--metrics" && i + 1 < argc) {
      cfg.metrics = argv[++i];
    } else if (arg == "--tolerance-per-channel" && i + 1 < argc) {
      cfg.tolerance.per_channel = std::stoi(argv[++i]);
    } else if (arg == "--max-diff-pct" && i + 1 < argc) {
      cfg.tolerance.max_diff_pct = std::stod(argv[++i]);
    } else if (arg == "--rmse" && i + 1 < argc) {
      cfg.tolerance.rmse = std::stod(argv[++i]);
    } else if (arg == "--quiet") {
      cfg.quiet = true;
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      return false;
    }
  }
  return !cfg.expected.empty() && !cfg.actual.empty();
}

std::string render_result(const CoinVisualTests::DiffMetrics& metrics) {
  std::ostringstream out;
  out << "diff_pct=" << metrics.diff_pct << "% "
      << "rmse=" << metrics.rmse << " "
      << "max_diff=" << metrics.max_abs_diff << " "
      << "failures=" << metrics.differing_pixels;
  return out.str();
}

int run_with_args(int argc, const char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
      print_usage();
      return 0;
    }
  }

  Config cfg;
  if (!parse_args(argc, argv, cfg)) {
    print_usage();
    return 2;
  }

  CoinVisualTests::Image expected;
  CoinVisualTests::Image actual;
  if (!CoinVisualTests::load_image(cfg.expected, expected)) {
    log_error(cfg, "Failed to load expected image: " + cfg.expected);
    return 2;
  }
  if (!CoinVisualTests::load_image(cfg.actual, actual)) {
    log_error(cfg, "Failed to load actual image: " + cfg.actual);
    return 2;
  }

  const auto result = CoinVisualTests::compare_images(expected, actual, cfg.tolerance);
  if (!cfg.quiet) {
    std::cout << "Compare result: " << render_result(result.metrics) << '\n';
  }

  if (!result.metrics.dimensions_match) {
    log_error(cfg, "Image dimensions differ: expected " +
                       std::to_string(expected.width) + "x" +
                       std::to_string(expected.height) + ", actual " +
                       std::to_string(actual.width) + "x" +
                       std::to_string(actual.height));
    if (!cfg.metrics.empty() &&
        !CoinVisualTests::write_metrics_json(cfg.metrics, result.metrics)) {
      log_error(cfg, "Failed to write metrics: " + cfg.metrics);
      return 2;
    }
    return 1;
  }

  if (!cfg.diff.empty()) {
    CoinVisualTests::Image diff;
    diff.width = result.metrics.width;
    diff.height = result.metrics.height;
    diff.pixels = result.diff_pixels;
    if (!CoinVisualTests::write_image(cfg.diff, diff)) {
      log_error(cfg, "Failed to write diff image: " + cfg.diff);
      return 2;
    }
  }
  if (!cfg.metrics.empty() && !CoinVisualTests::write_metrics_json(cfg.metrics, result.metrics)) {
    log_error(cfg, "Failed to write metrics: " + cfg.metrics);
    return 2;
  }
  return result.metrics.pass ? 0 : 1;
}

} // namespace

namespace CoinVisualTests {

int RunCompareCommand(int argc, const char* argv[]) {
  return run_with_args(argc, argv);
}

int RunCompareCommand(int argc, char** argv) {
  return run_with_args(argc, const_cast<const char**>(argv));
}

} // namespace CoinVisualTests
