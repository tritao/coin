#ifndef COIN_VISUAL_TESTS_SPEC_LOADER_H
#define COIN_VISUAL_TESTS_SPEC_LOADER_H

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Inventor/SbVec3f.h>

#include "RenderTolerance.h"

namespace CoinVisualTests {

struct CameraSpec {
  enum class Type { Perspective, Orthographic };
  Type type = Type::Perspective;
  SbVec3f position = SbVec3f(2.0f, 2.0f, 2.0f);
  SbVec3f target = SbVec3f(0.0f, 0.0f, 0.0f);
  SbVec3f up = SbVec3f(0.0f, 1.0f, 0.0f);
  float fov_deg = 45.0f;
  float height = 4.0f;
  float near = 0.1f;
  float far = 10.0f;
};

struct ViewportSpec {
  int width = 512;
  int height = 512;
  std::array<float, 4> background = {0.2f, 0.2f, 0.2f, 1.0f};
};

struct VisualTestSpec {
  std::string id;
  std::string scene;
  std::optional<CameraSpec> camera;
  ViewportSpec viewport;
  std::string baseline;
  std::map<std::string, std::string> renderer_baselines;
  ComparisonPolicy comparison = ComparisonPolicy::Default;
};

class SpecLoader {
public:
  VisualTestSpec load(const std::string& path) const;
  std::vector<std::pair<std::string, VisualTestSpec>> loadAllSpecs(const std::string& dir) const;
  bool loadSpecById(const std::string& dir,
                    const std::string& id,
                    std::pair<std::string, VisualTestSpec>& out) const;
};

} // namespace CoinVisualTests

#endif
