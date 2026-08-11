#include "rendering/CoinOffscreenGLCanvas.h"
#include "rendering/SoGLRenderBackend.h"

#include <Inventor/SoDB.h>
#include <Inventor/system/gl.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int skip(const char * reason)
{
  std::cout << "SKIP: " << reason << std::endl;
  return 77;
}

void set_environment(const char * name, const char * value)
{
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

}

static int
runTest()
{
  set_environment("COIN_EGL", "1");
  set_environment("EGL_PLATFORM", "surfaceless");
  set_environment("COIN_EGL_CORE_PROFILE", "1");

  SoDB::init();

  CoinOffscreenGLCanvas canvas;
  canvas.setWantedSize(SbVec2s(32, 32));
  if (canvas.activateGLContext() == 0) {
    return skip("core EGL offscreen context is unavailable");
  }

  SoGLRenderBackend backend;
  SoRenderBackendInitParams initparams = {};
  initparams.targetInfo.size = SbVec2s(32, 32);
  initparams.targetInfo.samples = 1;
  if (!backend.initialize(initparams)) {
    canvas.deactivateGLContext();
    return skip("core OpenGL draw-list backend could not initialize");
  }

  const uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };
  const float texcoords[] = {
    0.0f, 0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f
  };
  const float leftPositions[] = {
    -1.0f, -1.0f, 0.0f,
    -0.333f, -1.0f, 0.0f,
    -0.333f, 1.0f, 0.0f,
    -1.0f, 1.0f, 0.0f
  };
  const float middlePositions[] = {
    -0.333f, -1.0f, 0.0f,
     0.333f, -1.0f, 0.0f,
     0.333f, 1.0f, 0.0f,
    -0.333f, 1.0f, 0.0f
  };
  const float rightPositions[] = {
     0.333f, -1.0f, 0.0f,
     1.0f, -1.0f, 0.0f,
     1.0f, 1.0f, 0.0f,
     0.333f, 1.0f, 0.0f
  };
  const unsigned char luminance[] = { 220 };
  const unsigned char luminanceAlpha[] = { 100, 255 };

  auto makeCommand = [&](const float * positions,
                         const unsigned char * pixels,
                         int components,
                         const SbVec4f & diffuse) {
    SoRenderCommand command;
    command.modelMatrix.makeIdentity();
    command.geometry.topology = SO_TOPOLOGY_TRIANGLES;
    command.geometry.vertexCount = 4;
    command.geometry.indexCount = 6;
    command.geometry.positions = positions;
    command.geometry.indices = indices;
    command.geometry.vertexStride = sizeof(float) * 3;
    command.geometry.texcoords = pixels ? texcoords : nullptr;
    command.geometry.texcoordStride = sizeof(float) * 4;
    command.material.diffuse = diffuse;
    command.material.featureFlags = SO_FEAT_BASE_COLOR;
    if (pixels) {
      command.material.texture.pixels = pixels;
      command.material.texture.width = 1;
      command.material.texture.height = 1;
      command.material.texture.numComponents = components;
    }
    return command;
  };

  SoDrawList drawlist;
  drawlist.addCommand(makeCommand(leftPositions, luminance, 1,
                                  SbVec4f(1.0f, 1.0f, 1.0f, 1.0f)));
  drawlist.addCommand(makeCommand(middlePositions, luminanceAlpha, 2,
                                  SbVec4f(1.0f, 1.0f, 1.0f, 1.0f)));
  drawlist.addCommand(makeCommand(rightPositions, nullptr, 0,
                                  SbVec4f(1.0f, 0.0f, 0.0f, 1.0f)));

  SoRenderParams params = {};
  params.viewport = SbViewportRegion(32, 32);
  params.viewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(32, 32));
  params.viewMatrix.makeIdentity();
  params.projMatrix.makeIdentity();
  params.viewProjMatrix.makeIdentity();
  params.clearColor.setValue(0.0f, 0.0f, 0.0f, 1.0f);
  params.clearDepth = 1.0f;
  params.flags = SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH | SO_PARAM_SKIP_ID;

  int result = 0;
  if (!backend.render(drawlist, params)) {
    std::cerr << "FAIL: draw-list backend render failed" << std::endl;
    result = 1;
  }
  else {
    glFinish();
    std::vector<uint8_t> pixels(32 * 32 * 4, 0);
    canvas.readPixels(pixels.data(), SbVec2s(32, 32), 32, 4);
    auto pixelAt = [&](int x, int y) { return &pixels[(y * 32 + x) * 4]; };
    const uint8_t * leftPixel = pixelAt(5, 16);
    if (leftPixel[0] < 180 || std::abs(static_cast<int>(leftPixel[0]) - leftPixel[1]) > 5
        || std::abs(static_cast<int>(leftPixel[1]) - leftPixel[2]) > 5) {
      std::cerr << "FAIL: core draw-list L texture produced unexpected pixels" << std::endl;
      result = 1;
    }
    const uint8_t * middlePixel = pixelAt(16, 16);
    if (middlePixel[0] < 70 || middlePixel[0] > 130
        || std::abs(static_cast<int>(middlePixel[0]) - middlePixel[1]) > 5
        || std::abs(static_cast<int>(middlePixel[1]) - middlePixel[2]) > 5) {
      std::cerr << "FAIL: core draw-list LA texture produced unexpected pixels" << std::endl;
      result = 1;
    }
    const uint8_t * rightPixel = pixelAt(27, 16);
    if (rightPixel[0] < 200 || rightPixel[1] > 40 || rightPixel[2] > 40) {
      std::cerr << "FAIL: core draw-list plain rendering produced unexpected pixels" << std::endl;
      result = 1;
    }
  }

  backend.shutdown();
  canvas.deactivateGLContext();
  return result;
}

int
main()
{
  const int result = runTest();
  SoDB::finish();
  return result;
}
