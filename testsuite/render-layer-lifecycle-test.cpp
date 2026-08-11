#include "rendering/CoinOffscreenGLCanvas.h"
#include "rendering/SoRenderBackend.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoAction.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoSeparator.h>

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

SoSeparator * afterRoot = NULL;

void afterMain(void *, SoRenderManager *, SoAction * action)
{
  SoIRRenderAction * retained = dynamic_cast<SoIRRenderAction *>(action);
  if (retained && afterRoot) retained->traverseAdditionalRoot(afterRoot);
}

SoSeparator * coloredCube(float r, float g, float b)
{
  SoSeparator * root = new SoSeparator;
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(r, g, b);
  root->addChild(material);
  root->addChild(new SoCube);
  return root;
}

bool isColor(const uint8_t * pixel, int r, int g, int b)
{
  return std::abs(static_cast<int>(pixel[0]) - r) < 45 &&
         std::abs(static_cast<int>(pixel[1]) - g) < 45 &&
         std::abs(static_cast<int>(pixel[2]) - b) < 45;
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

  SoSeparator * scene = coloredCube(0.0f, 1.0f, 0.0f);
  SoSeparator * background = coloredCube(0.0f, 0.0f, 1.0f);
  afterRoot = coloredCube(1.0f, 0.0f, 0.0f);
  scene->ref();
  background->ref();
  afterRoot->ref();

  int result = 0;
  {
    SoRenderManager manager;
    SbViewportRegion testViewport(SbVec2s(32, 32));
    testViewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(32, 32));
    manager.setViewportRegion(testViewport);
    manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
    manager.setLightingMode(SoRenderManager::LightingMode::UNLIT);
    manager.setSceneGraph(scene);
    manager.setRenderLayerRoot(SoRenderManager::RENDER_LAYER_BACKGROUND, background);
    manager.addAfterMainSceneCallback(afterMain, NULL);
    manager.render(TRUE, TRUE);

    std::vector<uint8_t> pixels(32 * 32 * 4, 0);
    canvas.readPixels(pixels.data(), SbVec2s(32, 32), 32, 4);
    const uint8_t * pixel = &pixels[(16 * 32 + 16) * 4];
    if (!isColor(pixel, 255, 0, 0)) {
      std::cerr << "FAIL: after-main retained stage did not render after the scene" << std::endl;
      result = 1;
    }
    if (!manager.getRenderBackend() || !manager.getRenderBackend()->isInitialized()) {
      std::cerr << "FAIL: DrawList manager did not initialize its backend" << std::endl;
      result = 1;
    }

    canvas.deactivateGLContext();
    manager.discardRenderBackendResources();
    if (manager.getRenderBackend()->isInitialized()) {
      std::cerr << "FAIL: discarded backend still reports initialized" << std::endl;
      result = 1;
    }
    if (canvas.activateGLContext() == 0) {
      result = 1;
    }
    else {
      manager.render(TRUE, TRUE);
      if (!manager.getRenderBackend()->isInitialized()) {
        std::cerr << "FAIL: discarded backend was not reinitialized" << std::endl;
        result = 1;
      }
    }
  }

  afterRoot->unref();
  background->unref();
  scene->unref();
  afterRoot = NULL;
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
