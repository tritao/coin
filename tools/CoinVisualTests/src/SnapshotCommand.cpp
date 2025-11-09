#include "SnapshotCommand.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "SpecDirResolver.h"
#include "SpecLoader.h"

namespace fs = std::filesystem;

namespace {

struct SnapshotArgs {
  std::string spec_dir = CoinVisualTests::SpecDir::computeDefaultSpecDir();
  std::string spec_id;
  std::string spec_path;
};

bool looksLikeSceneFile(const std::string& value) {
  const fs::path path(value);
  return path.has_extension() && path.extension() == ".iv";
}

std::string default_output_file(const SnapshotArgs& args) {
  std::string base = "CoinVisualTests_snapshot";
  if (!args.spec_id.empty()) {
    base = args.spec_id;
  } else if (!args.spec_path.empty()) {
    base = fs::path(args.spec_path).stem().string();
  }
  return (fs::current_path() / (base + ".png")).string();
}

void print_usage() {
  std::cout << "Usage: CoinVisualTests snapshot [<spec_id>|<scene.iv>]"
            << " [--spec <spec.yml>] [--spec-dir <dir>] [--out <image.png>]"
            << " [--width N] [--height N] [--quiet]\n";
}

bool parse_args(int argc,
                const char* argv[],
                SnapshotArgs& args,
                CoinVisualTests::RenderCore::Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--out" && i + 1 < argc) {
      options.output_file = argv[++i];
    } else if (arg == "--spec" && i + 1 < argc) {
      args.spec_path = argv[++i];
    } else if (arg == "--spec-dir" && i + 1 < argc) {
      args.spec_dir = argv[++i];
    } else if (arg == "--width" && i + 1 < argc) {
      try {
        options.width = std::max(8, std::stoi(argv[++i]));
        options.width_override = true;
      } catch (...) {
        std::cerr << "Invalid width value.\n";
        return false;
      }
    } else if (arg == "--height" && i + 1 < argc) {
      try {
        options.height = std::max(8, std::stoi(argv[++i]));
        options.height_override = true;
      } catch (...) {
        std::cerr << "Invalid height value.\n";
        return false;
      }
    } else if (arg == "--quiet") {
      options.quiet = true;
    } else if (arg.rfind("--", 0) == 0) {
      std::cerr << "Unknown argument: " << arg << '\n';
      return false;
    } else {
      if (!args.spec_id.empty()) {
        std::cerr << "Only one positional spec identifier is allowed.\n";
        return false;
      }
      if (looksLikeSceneFile(arg)) {
        options.scene_file = arg;
      } else {
        args.spec_id = arg;
      }
    }
  }
  return true;
}

bool configure_spec(const std::string& path,
                    CoinVisualTests::RenderCore::Options& options) {
  try {
    const auto spec = CoinVisualTests::SpecLoader().load(path);
    options.scene_file = spec.scene;
    if (!options.width_override) {
      options.width = spec.viewport.width;
    }
    if (!options.height_override) {
      options.height = spec.viewport.height;
    }
    if (spec.camera) {
      options.camera = *spec.camera;
      options.camera_override = true;
    }
    options.clear_color = spec.viewport.background;
    return true;
  } catch (const std::exception& ex) {
    std::cerr << "Failed to load spec: " << ex.what() << '\n';
    return false;
  }
}

int run_with_args(int argc, const char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
      print_usage();
      return 0;
    }
  }

  CoinVisualTests::RenderCore::Options options;
  SnapshotArgs args;
  if (!parse_args(argc, argv, args, options)) {
    print_usage();
    return 1;
  }

  std::string resolved_spec;
  if (!args.spec_id.empty()) {
    try {
      std::pair<std::string, CoinVisualTests::VisualTestSpec> entry;
      if (!CoinVisualTests::SpecLoader().loadSpecById(args.spec_dir, args.spec_id, entry)) {
        std::cerr << "Spec '" << args.spec_id << "' not found in " << args.spec_dir << '\n';
        return 1;
      }
      resolved_spec = entry.first;
    } catch (const std::exception& ex) {
      std::cerr << "Failed to discover specs in " << args.spec_dir << ": " << ex.what() << '\n';
      return 1;
    }
  } else if (!args.spec_path.empty()) {
    resolved_spec = args.spec_path;
  }

  if (!resolved_spec.empty() && !configure_spec(resolved_spec, options)) {
    return 1;
  }
  if (options.scene_file.empty()) {
    print_usage();
    return 1;
  }
  if (options.output_file.empty()) {
    options.output_file = default_output_file(args);
  }

  CoinVisualTests::RenderCore core;
  return core.initialize(options) && core.run() ? 0 : 1;
}

} // namespace

namespace CoinVisualTests {

int RunSnapshotCommand(int argc, const char* argv[]) {
  return run_with_args(argc, argv);
}

int RunSnapshotCommand(int argc, char** argv) {
  return run_with_args(argc, const_cast<const char**>(argv));
}

} // namespace CoinVisualTests
