#include "SpecLoader.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

namespace CoinVisualTests {

namespace fs = std::filesystem;

namespace {

std::array<float, 3> toVec3(const YAML::Node& node,
                            const std::array<float, 3>& fallback) {
  if (!node) {
    return fallback;
  }
  std::array<float, 3> values;
  for (size_t i = 0; i < values.size(); ++i) {
    if (!node[i]) {
      throw std::runtime_error("Missing vector entry at index " + std::to_string(i));
    }
    values[i] = static_cast<float>(node[i].as<double>());
  }
  return values;
}

std::array<float, 4> toVec4(const YAML::Node& node,
                            const std::array<float, 4>& fallback) {
  if (!node) {
    return fallback;
  }
  std::array<float, 4> values;
  for (size_t i = 0; i < values.size(); ++i) {
    if (!node[i]) {
      throw std::runtime_error("Missing vector entry at index " + std::to_string(i));
    }
    values[i] = static_cast<float>(node[i].as<double>());
  }
  return values;
}

std::string requiredString(const YAML::Node& node, const char* key) {
  if (!node[key]) {
    throw std::runtime_error(std::string("Missing required key: ") + key);
  }
  return node[key].as<std::string>();
}

std::string resolveRelative(const fs::path& base, const std::string& target) {
  const fs::path path(target);
  return path.is_absolute() ? path.string() : (base / path).lexically_normal().string();
}

} // namespace

std::vector<std::pair<std::string, VisualTestSpec>>
SpecLoader::loadAllSpecs(const std::string& dir) const {
  const fs::path directory(dir);
  if (!fs::is_directory(directory)) {
    throw std::runtime_error("Spec directory does not exist: " + directory.string());
  }

  std::vector<fs::path> paths;
  for (const auto& entry : fs::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".yml") {
      paths.push_back(entry.path());
    }
  }
  std::sort(paths.begin(), paths.end());

  std::vector<std::pair<std::string, VisualTestSpec>> result;
  for (const auto& path : paths) {
    result.emplace_back(path.string(), load(path.string()));
  }
  return result;
}

bool SpecLoader::loadSpecById(const std::string& dir,
                              const std::string& id,
                              std::pair<std::string, VisualTestSpec>& out) const {
  for (const auto& entry : loadAllSpecs(dir)) {
    if (entry.second.id == id) {
      out = entry;
      return true;
    }
  }
  return false;
}

VisualTestSpec SpecLoader::load(const std::string& path) const {
  const YAML::Node root = YAML::LoadFile(path);
  const fs::path spec_dir = fs::path(path).parent_path();
  VisualTestSpec spec;

  spec.id = requiredString(root, "id");
  spec.scene = resolveRelative(spec_dir, requiredString(root, "scene"));

  if (root["camera"]) {
    CameraSpec camera;
    const auto& node = root["camera"];
    const std::string type = node["type"].as<std::string>("perspective");
    if (type == "orthographic") {
      camera.type = CameraSpec::Type::Orthographic;
    } else if (type != "perspective") {
      throw std::runtime_error("Unknown camera type in spec " + spec.id + ": " + type);
    }
    const auto toSbVec = [](const std::array<float, 3>& values) {
      return SbVec3f(values[0], values[1], values[2]);
    };
    camera.position = toSbVec(toVec3(node["position"], {2.0f, 2.0f, 2.0f}));
    camera.target = toSbVec(toVec3(node["target"], {0.0f, 0.0f, 0.0f}));
    camera.up = toSbVec(toVec3(node["up"], {0.0f, 1.0f, 0.0f}));
    if (camera.type == CameraSpec::Type::Orthographic) {
      if (!node["height"]) {
        throw std::runtime_error("Orthographic camera in spec " + spec.id +
                                 " requires 'height'");
      }
      camera.height = static_cast<float>(node["height"].as<double>());
    } else {
      if (!node["fov_deg"]) {
        throw std::runtime_error("Perspective camera in spec " + spec.id +
                                 " requires 'fov_deg'");
      }
      camera.fov_deg = static_cast<float>(node["fov_deg"].as<double>());
    }
    camera.near = static_cast<float>(node["near"].as<double>(0.1));
    camera.far = static_cast<float>(node["far"].as<double>(10.0));
    spec.camera = camera;
  }

  if (root["viewport"]) {
    const auto& viewport = root["viewport"];
    if (viewport["width"]) {
      spec.viewport.width = viewport["width"].as<int>();
    }
    if (viewport["height"]) {
      spec.viewport.height = viewport["height"].as<int>();
    }
    if (viewport["background"]) {
      spec.viewport.background = toVec4(viewport["background"],
                                        spec.viewport.background);
    }
  }

  if (root["baseline"]) {
    spec.baseline = resolveRelative(spec_dir, root["baseline"].as<std::string>());
  }
  if (root["baselines"]) {
    const YAML::Node baselines = root["baselines"];
    if (!baselines.IsMap()) {
      throw std::runtime_error("'baselines' must be a renderer-to-path map in spec " + spec.id);
    }
    for (const auto& entry : baselines) {
      const std::string renderer = entry.first.as<std::string>();
      if (renderer != "legacy" && renderer != "drawlist") {
        throw std::runtime_error("Unknown renderer baseline '" + renderer +
                                 "' in spec " + spec.id);
      }
      spec.renderer_baselines[renderer] = resolveRelative(
        spec_dir, entry.second.as<std::string>());
    }
  }
  if (spec.baseline.empty() && spec.renderer_baselines.empty()) {
    throw std::runtime_error("Missing required key: baseline or baselines");
  }
  const std::string comparison = root["compare"].as<std::string>("default");
  if (!parse_comparison_policy(comparison, spec.comparison)) {
    throw std::runtime_error("Unknown comparison policy in spec " + spec.id + ": " + comparison);
  }
  return spec;
}

} // namespace CoinVisualTests
