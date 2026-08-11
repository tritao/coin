#include "rendering/CoinOffscreenGLCanvas.h"
#include "rendering/SoGLRenderBackend.h"

#include <Inventor/SoDB.h>
#include <Inventor/system/gl.h>

#include <cstdlib>
#include <iostream>

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

int
main()
{
  set_environment("COIN_EGL", "1");
  set_environment("EGL_PLATFORM", "surfaceless");
  set_environment("COIN_EGL_CORE_PROFILE", "1");

  SoDB::init();
  CoinOffscreenGLCanvas canvas;
  canvas.setWantedSize(SbVec2s(32, 32));
  if (canvas.activateGLContext() == 0) {
    SoDB::finish();
    return skip("core EGL offscreen context is unavailable");
  }

  SoGLRenderBackend backend;
  SoRenderBackendInitParams initparams = {};
  initparams.targetInfo.size = SbVec2s(32, 32);
  initparams.targetInfo.samples = 1;
  if (!backend.initialize(initparams)) {
    canvas.deactivateGLContext();
    SoDB::finish();
    return skip("core OpenGL draw-list backend could not initialize");
  }

  const float positions[] = {
    -1.0f, -1.0f, 0.0f,
     1.0f, -1.0f, 0.0f,
     1.0f,  1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f
  };
  const uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };

  SoRenderCommand command;
  command.geometry.topology = SO_TOPOLOGY_TRIANGLES;
  command.geometry.vertexCount = 4;
  command.geometry.indexCount = 6;
  command.geometry.positions = positions;
  command.geometry.indices = indices;
  command.geometry.vertexStride = sizeof(float) * 3;
  command.material.diffuse.setValue(1.0f, 0.0f, 0.0f, 1.0f);
  command.material.featureFlags = SO_FEAT_BASE_COLOR;
  command.modelMatrix.makeIdentity();
  command.selection.selectWholeObject = true;
  command.selection.selectionColor.setValue(0.0f, 1.0f, 0.0f, 1.0f);
  command.pick.pickIdentity = "test-body";
  SoRenderElementRange range;
  range.elementType = SO_PICK_WHOLE_BODY;
  range.drawStart = 0;
  range.drawCount = 6;
  command.pick.elementRanges.push_back(range);

  SoDrawList drawlist;
  drawlist.addCommand(command);
  drawlist.buildPickLUT();
  if (drawlist.getPickLUT().size() != 1
      || drawlist.getPickLUT()[0].commandIndex != 0
      || drawlist.getPickLUT()[0].elementType != SO_PICK_WHOLE_BODY
      || drawlist.resolvePickIdentity(1) != "test-body"
      || drawlist.resolvePickIdentity(2) != "") {
    std::cerr << "FAIL: DrawList pick LUT did not resolve the expected identity" << std::endl;
    backend.shutdown();
    canvas.deactivateGLContext();
    SoDB::finish();
    return 1;
  }

  SoRenderParams params = {};
  params.viewport = SbViewportRegion(32, 32);
  params.viewMatrix.makeIdentity();
  params.projMatrix.makeIdentity();
  params.viewProjMatrix.makeIdentity();
  params.clearColor.setValue(0.0f, 0.0f, 0.0f, 1.0f);
  params.clearDepth = 1.0f;
  params.flags = SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH;

  int result = 0;
  if (!backend.render(drawlist, params)) {
    std::cerr << "FAIL: draw-list backend render failed" << std::endl;
    result = 1;
  }
  else {
    glFinish();
    uint8_t pixels[4] = { 0, 0, 0, 0 };
    canvas.readPixels(pixels, SbVec2s(16, 16), 1, 4);
    if (pixels[1] < 120 || pixels[0] > 80 || pixels[2] > 80) {
      std::cerr << "FAIL: selection overlay did not produce the expected green pixel" << std::endl;
      result = 1;
    }
    const uint32_t pickId = backend.pick(16, 16, 2);
    if (pickId != 1) {
      std::cerr << "FAIL: GPU pick did not return the expected LUT ID" << std::endl;
      result = 1;
    }
  }

  backend.shutdown();
  canvas.deactivateGLContext();
  SoDB::finish();
  return result;
}
