#include "rendering/CoinOffscreenGLCanvas.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoSeparator.h>

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

  SoSeparator * root = new SoSeparator;
  SoCube * cube = new SoCube;
  root->addChild(cube);
  root->ref();

  uint8_t pixel[4] = {0, 0, 0, 0};
  {
    SoRenderManager manager;
    manager.setViewportRegion(SbViewportRegion(SbVec2s(32, 32)));
    manager.setSceneGraph(root);
    manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
    manager.render(TRUE, TRUE);
    canvas.readPixels(pixel, SbVec2s(1, 1), 1, 4);
  }

  root->unref();
  canvas.deactivateGLContext();
  SoDB::finish();

  if (pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0) {
    std::cerr << "FAIL: manager DrawList pipeline produced no pixels" << std::endl;
    return 1;
  }
  return 0;
}
