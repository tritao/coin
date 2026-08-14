#ifndef COIN_VISUAL_TESTS_RENDER_DRIVER_H
#define COIN_VISUAL_TESTS_RENDER_DRIVER_H

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <Inventor/SbViewportRegion.h>

class SoNode;

namespace CoinVisualTests {

enum class RendererKind {
  Legacy,
  DrawList
};

enum class OpenGLProfile {
  Compatibility,
  Core
};

class RenderDriver {
public:
  virtual ~RenderDriver() = default;

  virtual bool initialize(int width, int height) = 0;
  virtual bool render(SoNode* scene,
                      const SbViewportRegion& viewport,
                      const std::array<float, 4>& clear_color,
                      std::vector<uint8_t>& pixels) = 0;
};

#if COIN_BUILD_LEGACY_GL_RENDERER
std::unique_ptr<RenderDriver> createLegacyGLDriver();
#endif
std::unique_ptr<RenderDriver> createDrawListDriver(OpenGLProfile profile);

} // namespace CoinVisualTests

#endif
