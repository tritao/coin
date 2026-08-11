#include "rendering/CoinOffscreenGLCanvas.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoAction.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
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
bool selectAfterRoot = false;

void addSelection(SoIRRenderAction * retained, const int commandIndex)
{
  if (!retained || commandIndex < 0 ||
      commandIndex >= retained->getDrawList().getNumCommands()) return;
  SoSelectionTarget target;
  target.commandIndex = commandIndex;
  target.color = SbColor4f(1.0f, 0.0f, 1.0f, 1.0f);
  retained->getMutableDrawList().getMutableSelectionState().selected.push_back(target);
}

void selectCommand(void * data, SoRenderManager *, SoAction * action)
{
  SoIRRenderAction * retained = dynamic_cast<SoIRRenderAction *>(action);
  if (retained && data) addSelection(retained, *static_cast<int *>(data));
}

void afterMain(void *, SoRenderManager *, SoAction * action)
{
  SoIRRenderAction * retained = dynamic_cast<SoIRRenderAction *>(action);
  if (retained && afterRoot) retained->traverseAdditionalRoot(afterRoot);
  if (retained && selectAfterRoot &&
      retained->getDrawList().getNumCommands() > 0) {
    addSelection(retained, retained->getDrawList().getNumCommands() - 1);
  }
}

void clearAfterMain(void *, SoRenderManager *, SoAction * action)
{
  SoIRRenderAction * retained = dynamic_cast<SoIRRenderAction *>(action);
  if (retained) retained->requestDepthClear();
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

SoSeparator * coloredQuad(float r, float g, float b, float z)
{
  SoSeparator * root = new SoSeparator;
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(r, g, b);
  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.setNum(4);
  coordinates->point.set1Value(0, SbVec3f(-0.85f, -0.85f, z));
  coordinates->point.set1Value(1, SbVec3f(0.85f, -0.85f, z));
  coordinates->point.set1Value(2, SbVec3f(0.85f, 0.85f, z));
  coordinates->point.set1Value(3, SbVec3f(-0.85f, 0.85f, z));
  SoFaceSet * faces = new SoFaceSet;
  faces->numVertices = 4;
  root->addChild(material);
  root->addChild(coordinates);
  root->addChild(faces);
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
#if defined(__linux__)
#endif

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

    // A selected main-stage object behind an opaque object must remain
    // occluded even when a later stage requests a depth clear.
    SoSeparator * occludedRoot = new SoSeparator;
    SoSeparator * occludingQuad = coloredQuad(0.0f, 1.0f, 0.0f, 0.0f);
    SoSeparator * occludedQuad = coloredQuad(1.0f, 0.0f, 0.0f, 0.5f);
    occludedRoot->addChild(occludingQuad);
    occludedRoot->addChild(occludedQuad);
    occludedRoot->ref();
    {
      SoRenderManager occludedManager;
      occludedManager.setViewportRegion(testViewport);
      occludedManager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
      occludedManager.setLightingMode(SoRenderManager::LightingMode::UNLIT);
      occludedManager.setSceneGraph(occludedRoot);
      int occludedCommand = 1;
      occludedManager.addAfterMainSceneCallback(selectCommand, &occludedCommand);
      occludedManager.addAfterMainSceneCallback(clearAfterMain, NULL);
      occludedManager.render(TRUE, TRUE);
      pixels.assign(32 * 32 * 4, 0);
      canvas->readPixels(pixels.data(), SbVec2s(32, 32), 32, 4);
      if (!isColor(&pixels[(16 * 32 + 16) * 4], 0, 255, 0)) {
        std::cerr << "FAIL: selected rear object escaped main-stage depth occlusion"
                  << std::endl;
        result = 1;
      }
    }
    occludedRoot->unref();

    // A selected after-main object must be visible after its stage's depth
    // segment has been cleared.
    selectAfterRoot = true;
    manager.render(TRUE, TRUE);
    selectAfterRoot = false;
    pixels.assign(32 * 32 * 4, 0);
    canvas->readPixels(pixels.data(), SbVec2s(32, 32), 32, 4);
    if (!isColor(&pixels[(16 * 32 + 16) * 4], 255, 0, 255)) {
      std::cerr << "FAIL: selected after-main object was not visible" << std::endl;
      result = 1;
    }

    SoSeparator * depthMain = new SoSeparator;
    depthMain->addChild(coloredQuad(0.0f, 0.0f, 1.0f, 0.0f));
    SoRenderLayerGroup * depthForeground = new SoRenderLayerGroup;
    depthForeground->layer = SoRenderLayerGroup::FOREGROUND;
    depthForeground->viewportOverride = TRUE;
    depthForeground->viewportPixels.setValue(8.0f, 8.0f, 16.0f, 16.0f);
    depthForeground->clearDepthBuffer = TRUE;
    depthForeground->addChild(coloredQuad(1.0f, 0.0f, 0.0f, 0.5f));
    depthMain->ref();
    depthForeground->ref();
    {
      SoRenderManager depthManager;
      depthManager.setViewportRegion(testViewport);
      depthManager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
      depthManager.setLightingMode(SoRenderManager::LightingMode::UNLIT);
      depthManager.setSceneGraph(depthMain);
      depthManager.setRenderLayerRoot(
        SoRenderManager::RENDER_LAYER_FOREGROUND, depthForeground);
      depthManager.render(TRUE, TRUE);
      pixels.assign(32 * 32 * 4, 0);
      canvas->readPixels(pixels.data(), SbVec2s(32, 32), 32, 4);
      if (!isColor(&pixels[(16 * 32 + 16) * 4], 255, 0, 0) ||
          !isColor(&pixels[(2 * 32 + 2) * 4], 0, 0, 255)) {
        std::cerr << "FAIL: scoped depth clear did not affect only its viewport"
                  << std::endl;
        result = 1;
      }
    }
    depthForeground->unref();
    depthMain->unref();

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
    // next retained frame must rebuild its layer-rendering resources.
    manager.releaseRenderBackendResources();
    manager.render(TRUE, TRUE);
    pixels.assign(32 * 32 * 4, 0);
    canvas->readPixels(pixels.data(), SbVec2s(32, 32), 32, 4);
    if (!isColor(&pixels[(5 * 32 + 5) * 4], 255, 255, 0)) {
      std::cerr << "FAIL: release/reinitialize did not restore retained rendering" << std::endl;
      result = 1;
    }

    // Destroy the actual context before asking the backend to discard its
    // resources. This exercises the no-GL discard contract.
    canvas->deactivateGLContext();
    canvas.reset();
    manager.discardRenderBackendResources();
    canvas.reset(new CoinOffscreenGLCanvas);
    canvas->setWantedSize(SbVec2s(32, 32));
    if (canvas->activateGLContext() == 0) {
      result = 1;
    }
    else {
      manager.render(TRUE, TRUE);
      pixels.assign(32 * 32 * 4, 0);
      canvas->readPixels(pixels.data(), SbVec2s(32, 32), 32, 4);
      if (!isColor(&pixels[(5 * 32 + 5) * 4], 255, 255, 0)) {
        std::cerr << "FAIL: discarded backend was not reinitialized" << std::endl;
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
