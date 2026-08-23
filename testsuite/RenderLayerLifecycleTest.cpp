#include "support/GLTestContext.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoPath.h>
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

#include <iostream>
#include <memory>
#include <vector>

namespace {

int skip(const char * reason)
{
  std::cout << "SKIP: " << reason << std::endl;
  return 77;
}

SoSeparator * afterRoot = NULL;
SoPath * capturedAfterPath = NULL;

void afterMain(void *, SoRenderManager *, SoAction * action)
{
  SoIRRenderAction * retained = dynamic_cast<SoIRRenderAction *>(action);
  if (retained && afterRoot) retained->traverseAdditionalRoot(afterRoot);
  if (retained && retained->getDrawList().getNumCommands() > 0) {
    const SoPath * commandPath = retained->getCommandPath(
      retained->getDrawList().getNumCommands() - 1);
    if (commandPath) {
      if (capturedAfterPath) capturedAfterPath->unref();
      capturedAfterPath = commandPath->copy();
      capturedAfterPath->ref();
    }
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
  SoDB::init();
  GLTestContextConfig config;
  config.profile = GLTestProfile::Core;
  config.major = 3;
  config.minor = 3;
  config.width = 32;
  config.height = 32;
  std::unique_ptr<GLTestContext> context(new GLTestContext);
  if (!context->initialize(config)) {
    return skip("core GLFW OpenGL context is unavailable");
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
    pixels = context->readPixels();
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
      pixels = context->readPixels();
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
    pixels = context->readPixels();
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
    pixels = context->readPixels();
    if (!isColor(&pixels[(5 * 32 + 5) * 4], 255, 255, 0)) {
      std::cerr << "FAIL: release/reinitialize did not restore retained rendering" << std::endl;
      result = 1;
    }

    // Destroy the actual context before asking the backend to discard its
    // resources. This exercises the no-GL discard contract.
    context->shutdown();
    context.reset();
    manager.discardRenderBackendResources();
    context.reset(new GLTestContext);
    if (!context->initialize(config)) {
      result = 1;
    }
    else {
      manager.render(TRUE, TRUE);
      pixels.assign(32 * 32 * 4, 0);
      pixels = context->readPixels();
      if (!isColor(&pixels[(5 * 32 + 5) * 4], 255, 255, 0)) {
        std::cerr << "FAIL: discarded backend was not reinitialized" << std::endl;
        result = 1;
      }
    }
    // Destruction after context loss must discard rather than issue GL calls.
    context.reset();
  }

  afterRoot->unref();
  if (capturedAfterPath) capturedAfterPath->unref();
  background->unref();
  scene->unref();
  foreground->unref();
  afterRoot = NULL;
  context.reset();
  return result;
}

int
main()
{
  const int result = runTest();
  SoDB::finish();
  return result;
}
