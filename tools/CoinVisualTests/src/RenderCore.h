#ifndef COIN_VISUAL_TESTS_RENDER_CORE_H
#define COIN_VISUAL_TESTS_RENDER_CORE_H

#include <array>
#include <memory>
#include <string>

#include "SpecLoader.h"

namespace CoinVisualTests {

class RenderCore {
public:
  struct Options {
    std::string scene_file;
    std::string output_file;
    int width = 512;
    int height = 512;
    bool width_override = false;
    bool height_override = false;
    bool camera_override = false;
    bool quiet = false;
    CameraSpec camera;
    std::array<float, 4> clear_color = {0.2f, 0.2f, 0.2f, 1.0f};
  };

  RenderCore();
  ~RenderCore();

  bool initialize(const Options& options);
  bool run();

private:
  RenderCore(const RenderCore&) = delete;
  RenderCore& operator=(const RenderCore&) = delete;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace CoinVisualTests

#endif
