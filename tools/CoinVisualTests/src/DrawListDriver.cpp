#include "RenderDriver.h"

#include "DrawListFrameContract.h"
#include "GLFWBackend.h"

#include "rendering/SoGLRenderBackend.h"

#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/rendering/SoRenderIR.h>

#include <iostream>

namespace CoinVisualTests {

namespace {

class DrawListDriver final : public RenderDriver {
public:
  explicit DrawListDriver(OpenGLProfile profile) : profile_(profile) {}

  ~DrawListDriver() override {
    if (renderer_.isInitialized()) {
      renderer_.shutdown();
    }
  }

  bool initialize(int width, int height) override {
    GLFWBackendConfig config;
    config.width = width;
    config.height = height;
    config.profile = profile_;
    if (!backend_.init(config)) {
      return false;
    }

    SoRenderBackendInitParams params;
    if (!renderer_.initialize(params)) {
      std::cerr << "Failed to initialize the DrawList OpenGL backend.\n";
      return false;
    }
    return true;
  }

  bool render(SoNode* scene,
              const SbViewportRegion& viewport,
              const std::array<float, 4>& clear_color,
              std::vector<uint8_t>& pixels) override {
    if (!scene || !backend_.initialized() || !renderer_.isInitialized()) {
      return false;
    }

    backend_.bindFramebuffer();

    SoIRRenderAction action(viewport);
    action.apply(scene);
    SoDrawList& drawlist = action.getMutableDrawList();

    SbMatrix view;
    SbMatrix projection;
    std::string matrix_error;
    if (!extractFrameMatrices(drawlist, view, projection, matrix_error)) {
      std::cerr << matrix_error << '\n';
      return false;
    }
    drawlist.buildSortedOrder(view);

    SoRenderParams params;
    params.viewport = viewport;
    params.viewMatrix = view;
    params.projMatrix = projection;
    params.clearColor.setValue(clear_color[0], clear_color[1],
                               clear_color[2], clear_color[3]);
    params.clearDepth = 1.0f;
    params.flags = SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH;

    if (!renderer_.render(drawlist, params)) {
      return false;
    }
    if (!backend_.readPixels(pixels)) {
      std::cerr << "Failed to read pixels from the DrawList framebuffer.\n";
      return false;
    }
    return true;
  }

private:
  OpenGLProfile profile_;
  GLFWBackend backend_;
  SoGLRenderBackend renderer_;
};

} // namespace

std::unique_ptr<RenderDriver>
createDrawListDriver(OpenGLProfile profile) {
  return std::unique_ptr<RenderDriver>(new DrawListDriver(profile));
}

} // namespace CoinVisualTests
