#include "RunCommand.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "CompareCommand.h"
#include "SpecDirResolver.h"
#include "SpecLoader.h"
#include "SnapshotCommand.h"

namespace fs = std::filesystem;

namespace {

struct Options {
  std::string spec_dir = CoinVisualTests::SpecDir::computeDefaultSpecDir();
  std::string artifacts_dir = CoinVisualTests::SpecDir::computeArtifactsBase();
  bool verbose = false;
  std::set<std::string> only_ids;
  bool update_baselines = false;
  CoinVisualTests::RendererKind renderer = CoinVisualTests::RendererKind::Legacy;
  CoinVisualTests::OpenGLProfile gl_profile = CoinVisualTests::OpenGLProfile::Compatibility;
};

using SpecEntry = std::pair<std::string, CoinVisualTests::VisualTestSpec>;

struct MetricsSummary {
  bool valid = false;
  double diff_pct = 0.0;
  double rmse = 0.0;
  int max_abs_diff = 0;
};

constexpr const char* kColorReset = "\033[0m";
constexpr const char* kColorPass = "\033[32m";
constexpr const char* kColorFail = "\033[31m";
constexpr const char* kColorInfo = "\033[34m";

struct OutputResult {
  bool success = false;
  bool baseline_updated = false;
  std::string baseline;
  std::string actual;
  std::string diff;
  std::string metrics;
  MetricsSummary metrics_summary;
};

const char* renderer_name(CoinVisualTests::RendererKind renderer) {
  return renderer == CoinVisualTests::RendererKind::Legacy ? "legacy" : "drawlist";
}

std::string baseline_for(const CoinVisualTests::VisualTestSpec& spec,
                         CoinVisualTests::RendererKind renderer) {
  const auto found = spec.renderer_baselines.find(renderer_name(renderer));
  return found == spec.renderer_baselines.end() ? spec.baseline : found->second;
}

bool has_renderer_baseline(const CoinVisualTests::VisualTestSpec& spec,
                           CoinVisualTests::RendererKind renderer) {
  return spec.renderer_baselines.find(renderer_name(renderer)) !=
    spec.renderer_baselines.end();
}

bool ensure_directory(const std::string& path) {
  if (path.empty()) {
    return true;
  }
  try {
    fs::create_directories(path);
    return fs::is_directory(path);
  } catch (const fs::filesystem_error&) {
    return false;
  }
}

bool ensure_parent(const std::string& path) {
  const fs::path parent = fs::path(path).parent_path();
  return parent.empty() || ensure_directory(parent.string());
}

bool copy_file(const std::string& src, const std::string& dst) {
  if (!ensure_parent(dst)) {
    return false;
  }
  std::ifstream in(src, std::ios::binary);
  std::ofstream out(dst, std::ios::binary);
  if (!in || !out) {
    return false;
  }
  out << in.rdbuf();
  return out.good();
}

std::string extract_json_value(const std::string& line) {
  const auto colon = line.find(':');
  if (colon == std::string::npos) {
    return {};
  }
  const auto start = line.find_first_not_of(" \t\r\n", colon + 1);
  const auto end = line.find_last_not_of(" \t\r\n,");
  if (start == std::string::npos || end < start) {
    return {};
  }
  return line.substr(start, end - start + 1);
}

MetricsSummary parse_metrics(const std::string& path) {
  MetricsSummary summary;
  std::ifstream in(path);
  std::string line;
  while (in && std::getline(in, line)) {
    if (line.find("\"diff_pct\"") != std::string::npos) {
      summary.diff_pct = std::stod(extract_json_value(line));
      summary.valid = true;
    } else if (line.find("\"rmse\"") != std::string::npos) {
      summary.rmse = std::stod(extract_json_value(line));
      summary.valid = true;
    } else if (line.find("\"max_abs_diff\"") != std::string::npos) {
      summary.max_abs_diff = std::stoi(extract_json_value(line));
      summary.valid = true;
    }
  }
  return summary;
}

std::string artifact_variant(CoinVisualTests::RendererKind renderer,
                             CoinVisualTests::OpenGLProfile profile) {
  if (renderer == CoinVisualTests::RendererKind::Legacy) {
    return "legacy";
  }
  return profile == CoinVisualTests::OpenGLProfile::Core
    ? "drawlist-core"
    : "drawlist-compat";
}

bool parse_options(int argc, const char** argv, Options& opts) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--spec-dir" && i + 1 < argc) {
      opts.spec_dir = argv[++i];
    } else if (arg == "--artifacts-dir" && i + 1 < argc) {
      opts.artifacts_dir = argv[++i];
    } else if (arg == "--verbose") {
      opts.verbose = true;
    } else if (arg == "--only" && i + 1 < argc) {
      std::istringstream values(argv[++i]);
      std::string value;
      while (std::getline(values, value, ',')) {
        if (!value.empty()) {
          opts.only_ids.insert(value);
        }
      }
    } else if (arg == "--update-baselines") {
      opts.update_baselines = true;
    } else if (arg == "--renderer" && i + 1 < argc) {
      const std::string renderer = argv[++i];
      if (renderer == "legacy") {
        opts.renderer = CoinVisualTests::RendererKind::Legacy;
      } else if (renderer == "drawlist") {
        opts.renderer = CoinVisualTests::RendererKind::DrawList;
      } else {
        std::cerr << "Unknown renderer: " << renderer << '\n';
        return false;
      }
    } else if (arg == "--gl-profile" && i + 1 < argc) {
      const std::string profile = argv[++i];
      if (profile == "compat") {
        opts.gl_profile = CoinVisualTests::OpenGLProfile::Compatibility;
      } else if (profile == "core") {
        opts.gl_profile = CoinVisualTests::OpenGLProfile::Core;
      } else {
        std::cerr << "Unknown OpenGL profile: " << profile << '\n';
        return false;
      }
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      return false;
    }
  }
  return true;
}

void print_usage() {
  std::cerr << "Usage: CoinVisualTests run [--spec-dir <dir>]"
            << " [--artifacts-dir <dir>] [--only id1,id2]"
            << " [--renderer legacy|drawlist] [--gl-profile compat|core]"
            << " [--verbose] [--update-baselines]\n";
  std::cerr << "Defaults: spec-dir=" << CoinVisualTests::SpecDir::computeDefaultSpecDir()
            << " artifacts-dir=" << CoinVisualTests::SpecDir::computeArtifactsBase() << '\n';
}

int invoke_snapshot(const std::string& spec_path,
                    const std::string& actual_path,
                    bool quiet,
                    CoinVisualTests::RendererKind renderer,
                    CoinVisualTests::OpenGLProfile gl_profile) {
  std::vector<std::string> args = {"snapshot", "--spec", spec_path, "--out", actual_path};
  args.emplace_back("--renderer");
  args.emplace_back(renderer == CoinVisualTests::RendererKind::Legacy ? "legacy" : "drawlist");
  args.emplace_back("--gl-profile");
  args.emplace_back(gl_profile == CoinVisualTests::OpenGLProfile::Core ? "core" : "compat");
  if (quiet) {
    args.emplace_back("--quiet");
  }
  std::vector<const char*> c_args;
  for (const auto& arg : args) {
    c_args.push_back(arg.c_str());
  }
  return CoinVisualTests::RunSnapshotCommand(static_cast<int>(c_args.size()), c_args.data());
}

int invoke_compare(const std::string& expected,
                   const std::string& actual,
                   const std::string& diff,
                   const std::string& metrics,
                   const CoinVisualTests::ToleranceSpec& tolerance) {
  std::vector<std::string> args = {"compare", "--expected", expected, "--actual", actual,
                                   "--diff", diff, "--metrics", metrics, "--quiet",
                                   "--tolerance-per-channel", std::to_string(tolerance.per_channel),
                                   "--max-diff-pct", std::to_string(tolerance.max_diff_pct),
                                   "--rmse", std::to_string(tolerance.rmse)};
  std::vector<const char*> c_args;
  for (const auto& arg : args) {
    c_args.push_back(arg.c_str());
  }
  return CoinVisualTests::RunCompareCommand(static_cast<int>(c_args.size()), c_args.data());
}

OutputResult run_single_output(const std::string& spec_path,
                               const CoinVisualTests::VisualTestSpec& spec,
                               const Options& opts) {
  OutputResult result;
  result.baseline = baseline_for(spec, opts.renderer);
  if (result.baseline.empty()) {
    std::cerr << "No baseline configured for renderer '" << renderer_name(opts.renderer)
              << "' in spec " << spec.id << '\n';
    return result;
  }
  const std::string output_dir = opts.artifacts_dir + "/gl/" +
                                 artifact_variant(opts.renderer, opts.gl_profile);
  const std::string actual = output_dir + "/" + spec.id + ".actual.png";
  const std::string diff = output_dir + "/" + spec.id + ".diff.png";
  const std::string metrics = output_dir + "/" + spec.id + ".metrics.json";
  if (!ensure_directory(output_dir)) {
    std::cerr << "Failed to create render artifact directory: " << output_dir << '\n';
    return result;
  }
  result.actual = actual;
  result.diff = diff;
  result.metrics = metrics;

  if (invoke_snapshot(spec_path, actual, !opts.verbose,
                      opts.renderer, opts.gl_profile) != 0) {
    std::cerr << "Snapshot failed for spec " << spec.id << '\n';
    return result;
  }

  if (opts.update_baselines) {
    if (opts.renderer != CoinVisualTests::RendererKind::Legacy &&
        !has_renderer_baseline(spec, opts.renderer)) {
      std::cerr << "No renderer-qualified baseline configured for '"
                << renderer_name(opts.renderer) << "' in spec " << spec.id << '\n';
      return result;
    }
    if (!copy_file(actual, result.baseline)) {
      std::cerr << "Failed to update baseline " << result.baseline << '\n';
      return result;
    }
    result.baseline_updated = true;
    result.success = true;
    return result;
  }

  const auto tolerance = CoinVisualTests::tolerance_for_policy(spec.comparison);
  const int compare_rc = invoke_compare(result.baseline, actual, diff, metrics, tolerance);
  result.metrics_summary = parse_metrics(metrics);
  result.success = compare_rc == 0;
  return result;
}

int run_spec(const SpecEntry& entry,
             const Options& opts,
             int& passed,
             int& failed,
             int& updated) {
  const auto& spec_path = entry.first;
  const auto& spec = entry.second;
  if (!opts.only_ids.empty() && opts.only_ids.count(spec.id) == 0) {
    return 0;
  }

  if (opts.verbose) {
    std::cout << "\n" << kColorInfo << "[CoinVisualTests] Running spec '" << spec.id
              << "'" << kColorReset << '\n';
  }
  const OutputResult result = run_single_output(spec_path, spec, opts);
  if (result.success && result.baseline_updated) {
    ++updated;
    ++passed;
    std::cout << kColorInfo << "[CoinVisualTests] Updated baseline for spec '" << spec.id
              << "' -> " << result.baseline << kColorReset << '\n';
    return 0;
  }

  if (result.success) {
    ++passed;
    if (!opts.verbose) {
      std::cout << kColorPass << "PASS" << kColorReset << " " << spec.id << '\n';
    }
    return 0;
  }

  ++failed;
  std::ostringstream stats;
  if (result.metrics_summary.valid) {
    stats << "diff_pct=" << result.metrics_summary.diff_pct << "% "
          << "rmse=" << result.metrics_summary.rmse << " "
          << "max_diff=" << result.metrics_summary.max_abs_diff;
  } else {
    stats << "metrics=n/a";
  }
  std::cout << kColorFail << "FAIL" << kColorReset << " " << spec.id
            << " (" << stats.str() << ")\n"
            << "  actual  " << result.actual << "\n"
            << "  diff    " << result.diff << "\n"
            << "  metrics " << result.metrics << '\n';
  return 1;
}

int run_with_options(int argc, const char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
      print_usage();
      return 0;
    }
  }

  Options opts;
  if (!parse_options(argc, argv, opts)) {
    print_usage();
    return 1;
  }
  if (opts.renderer == CoinVisualTests::RendererKind::Legacy &&
      opts.gl_profile == CoinVisualTests::OpenGLProfile::Core) {
    std::cerr << "LegacyGL rendering requires a compatibility OpenGL profile.\n";
    return 1;
  }
#if !COIN_BUILD_LEGACY_GL_RENDERER
  if (opts.renderer == CoinVisualTests::RendererKind::Legacy) {
    std::cerr << "LegacyGL baseline authoring and rendering are unavailable in this build.\n";
    return 1;
  }
#endif
  if (!ensure_directory(opts.artifacts_dir)) {
    std::cerr << "Failed to create artifacts dir: " << opts.artifacts_dir << '\n';
    return 1;
  }

  std::vector<SpecEntry> specs;
  try {
    specs = CoinVisualTests::SpecLoader().loadAllSpecs(opts.spec_dir);
  } catch (const std::exception& ex) {
    std::cerr << "Failed to load specs from " << opts.spec_dir << ": " << ex.what() << '\n';
    return 1;
  }
  if (specs.empty()) {
    std::cerr << "No specs found in " << opts.spec_dir << '\n';
    return 1;
  }
  std::sort(specs.begin(), specs.end(), [](const SpecEntry& left, const SpecEntry& right) {
    return left.second.id < right.second.id;
  });

  std::set<std::string> available_ids;
  for (const auto& entry : specs) {
    available_ids.insert(entry.second.id);
  }
  for (const auto& requested : opts.only_ids) {
    if (available_ids.count(requested) == 0) {
      std::cerr << "Unknown visual spec id: " << requested << '\n';
      return 1;
    }
  }

  if (opts.update_baselines && opts.renderer != CoinVisualTests::RendererKind::Legacy) {
    for (const auto& entry : specs) {
      if (!opts.only_ids.empty() && opts.only_ids.count(entry.second.id) == 0) {
        continue;
      }
      if (!has_renderer_baseline(entry.second, opts.renderer)) {
        std::cerr << "DrawList baseline update requires a renderer-qualified baseline for spec "
                  << entry.second.id << '\n';
        return 1;
      }
    }
  }

  int passed = 0;
  int failed = 0;
  int updated = 0;
  for (const auto& entry : specs) {
    run_spec(entry, opts, passed, failed, updated);
  }

  const bool success = failed == 0;
  std::cout << '\n' << (success ? kColorPass : kColorFail)
            << (success ? "PASSED" : "FAILED") << kColorReset
            << " specs: " << passed << " passed, " << failed << " failed";
  if (opts.update_baselines) {
    std::cout << " (updated " << updated << ")";
  }
  std::cout << '\n';
  return success ? 0 : 1;
}

} // namespace

namespace CoinVisualTests {

int RunRunCommand(int argc, const char* argv[]) {
  return run_with_options(argc, argv);
}

int RunRunCommand(int argc, char** argv) {
  return run_with_options(argc, const_cast<const char**>(argv));
}

} // namespace CoinVisualTests
