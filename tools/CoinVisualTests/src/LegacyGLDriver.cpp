#include "RenderDriver.h"

#include "GLFWBackend.h"

#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/system/gl.h>

#include <iostream>

namespace CoinVisualTests {

namespace {

class LegacyGLDriver final : public RenderDriver {
public:
  bool initialize(int width, int height) override {
    GLFWBackendConfig config;
    config.width = width;
    config.height = height;
    return backend_.init(config);
  }

  bool render(SoNode* scene,
              const SbViewportRegion& viewport,
              const std::array<float, 4>& clear_color,
              std::vector<uint8_t>& pixels) override {
    if (!scene || !backend_.initialized()) {
      return false;
    }

    backend_.bindFramebuffer();
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    SoGLRenderAction action(viewport);
    action.apply(scene);

    if (!backend_.readPixels(pixels)) {
      std::cerr << "Failed to read pixels from the LegacyGL framebuffer.\n";
      return false;
    }
    return true;
  }

private:
  GLFWBackend backend_;
};

} // namespace

std::unique_ptr<RenderDriver> createLegacyGLDriver() {
  return std::unique_ptr<RenderDriver>(new LegacyGLDriver);
}

} // namespace CoinVisualTests
