#include "rendering/CoinOffscreenGLCanvas.h"
#include "rendering/SoGLRenderBackend.h"
#include "rendering/SoRenderBackend.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoAction.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoRenderLayerGroup.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>

#include <cstdlib>
#include <iostream>
#include <memory>
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
  std::unique_ptr<CoinOffscreenGLCanvas> canvas(new CoinOffscreenGLCanvas);
  canvas->setWantedSize(SbVec2s(32, 32));
  if (canvas->activateGLContext() == 0) {
    return skip("core EGL offscreen context is unavailable");
  }

  SoRenderLayerGroup * scene = new SoRenderLayerGroup;
  scene->layer = SoRenderLayerGroup::INHERIT;
  scene->viewportOverride = TRUE;
  scene->viewportPixels.setValue(22.0f, 22.0f, 10.0f, 10.0f);
  scene->addChild(coloredCube(0.0f, 1.0f, 0.0f));
  SoSeparator * background = coloredCube(0.0f, 0.0f, 1.0f);
  afterRoot = new SoSeparator;
  SoTransform * afterTransform = new SoTransform;
  afterTransform->scaleFactor.setValue(0.4f, 0.4f, 0.4f);
  afterRoot->addChild(afterTransform);
  afterRoot->addChild(coloredCube(1.0f, 0.0f, 0.0f));
  SoRenderLayerGroup * foreground = new SoRenderLayerGroup;
  foreground->layer = SoRenderLayerGroup::FOREGROUND;
  foreground->viewportOverride = TRUE;
  foreground->viewportPixels.setValue(0.0f, 0.0f, 10.0f, 10.0f);
  foreground->addChild(coloredCube(1.0f, 1.0f, 0.0f));
  scene->ref();
  background->ref();
  afterRoot->ref();
  foreground->ref();

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
    manager.setRenderLayerRoot(SoRenderManager::RENDER_LAYER_FOREGROUND, foreground);
    manager.addAfterMainSceneCallback(afterMain, NULL);
    manager.render(TRUE, TRUE);

    std::vector<uint8_t> pixels(32 * 32 * 4, 0);
    canvas->readPixels(pixels.data(), SbVec2s(32, 32), 32, 4);
    const uint8_t * pixel = &pixels[(16 * 32 + 16) * 4];
    if (!isColor(pixel, 255, 0, 0)) {
      std::cerr << "FAIL: after-main retained stage did not render after the scene" << std::endl;
      result = 1;
    }
    const uint8_t * foregroundPixel = &pixels[(5 * 32 + 5) * 4];
    if (!isColor(foregroundPixel, 255, 255, 0)) {
      std::cerr << "FAIL: foreground layer did not render in its local viewport" << std::endl;
      result = 1;
    }
    const uint8_t * mainPixel = &pixels[(26 * 32 + 26) * 4];
    if (!isColor(mainPixel, 0, 255, 0)) {
      std::cerr << "FAIL: Main/INHERIT layer did not render in its local viewport" << std::endl;
      result = 1;
    }
    const SoDrawList & stagedCommands = manager.getIRRenderAction()->getDrawList();
    if (stagedCommands.getNumCommands() < 4 ||
        stagedCommands.getCommand(0).stage != SoRenderStage::Background ||
        stagedCommands.getCommand(1).stage != SoRenderStage::Main ||
        stagedCommands.getCommand(2).stage != SoRenderStage::AfterMain ||
        stagedCommands.getCommand(3).stage != SoRenderStage::Foreground ||
        !(stagedCommands.getCommand(0).material.featureFlags & SO_FEAT_BASE_COLOR) ||
        !(stagedCommands.getCommand(1).material.featureFlags & SO_FEAT_BASE_COLOR) ||
        !(stagedCommands.getCommand(2).material.featureFlags & SO_FEAT_BASE_COLOR) ||
        !(stagedCommands.getCommand(3).material.featureFlags & SO_FEAT_BASE_COLOR)) {
      std::cerr << "FAIL: retained manager stages were not ordered Background/Main/AfterMain/Foreground" << std::endl;
      result = 1;
    }
    if (!manager.getRenderBackend() || !manager.getRenderBackend()->isInitialized()) {
      std::cerr << "FAIL: DrawList manager did not initialize its backend" << std::endl;
      result = 1;
    }

    SoGLRenderBackend * backend = dynamic_cast<SoGLRenderBackend *>(
      manager.getRenderBackend());
    if (!backend) {
      std::cerr << "FAIL: DrawList manager did not expose its GL backend" << std::endl;
      result = 1;
    }
    const uint32_t expectedPick = backend ? backend->pick(16, 16, 2) : 0;
    if (expectedPick == 0) {
      std::cerr << "FAIL: initial manager render did not produce a pick ID" << std::endl;
      result = 1;
    }

    // Replacing the foreground root must invalidate the retained frame; the
    // old yellow viewport must disappear and return when the root is restored.
    manager.setRenderLayerRoot(SoRenderManager::RENDER_LAYER_FOREGROUND, NULL);
    manager.render(TRUE, TRUE);
    pixels.assign(32 * 32 * 4, 0);
    canvas->readPixels(pixels.data(), SbVec2s(32, 32), 32, 4);
    if (isColor(&pixels[(5 * 32 + 5) * 4], 255, 255, 0)) {
      std::cerr << "FAIL: removing the foreground root did not invalidate retained rendering" << std::endl;
      result = 1;
    }
    manager.setRenderLayerRoot(SoRenderManager::RENDER_LAYER_FOREGROUND, foreground);
    manager.render(TRUE, TRUE);

    // A normal release destroys resources while the context is alive. The
    // same backend object must rebuild them and preserve the pick result.
    manager.releaseRenderBackendResources();
    manager.render(TRUE, TRUE);
    backend = dynamic_cast<SoGLRenderBackend *>(manager.getRenderBackend());
    if (!backend || backend->pick(16, 16, 2) != expectedPick) {
      std::cerr << "FAIL: release/reinitialize changed the exact pick ID" << std::endl;
      result = 1;
    }

    // Destroy the actual context before asking the backend to discard its
    // resources. This exercises the no-GL discard contract.
    canvas->deactivateGLContext();
    canvas.reset();
    manager.discardRenderBackendResources();
    if (manager.getRenderBackend()->isInitialized()) {
      std::cerr << "FAIL: discarded backend still reports initialized" << std::endl;
      result = 1;
    }
    canvas.reset(new CoinOffscreenGLCanvas);
    canvas->setWantedSize(SbVec2s(32, 32));
    if (canvas->activateGLContext() == 0) {
      result = 1;
    }
    else {
      manager.render(TRUE, TRUE);
      backend = dynamic_cast<SoGLRenderBackend *>(manager.getRenderBackend());
      if (!backend || !manager.getRenderBackend()->isInitialized()) {
        std::cerr << "FAIL: discarded backend was not reinitialized" << std::endl;
        result = 1;
      }
      else if (backend->pick(16, 16, 2) != expectedPick) {
        std::cerr << "FAIL: context loss/recovery changed the exact pick ID" << std::endl;
        result = 1;
      }
    }
    // Destruction after context loss must discard rather than issue GL calls.
    canvas->deactivateGLContext();
    canvas.reset();
  }

  afterRoot->unref();
  background->unref();
  scene->unref();
  foreground->unref();
  afterRoot = NULL;
  if (canvas) {
    canvas->deactivateGLContext();
    canvas.reset();
  }
  return result;
}

int
main()
{
  const int result = runTest();
  SoDB::finish();
  return result;
}
