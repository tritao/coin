#include "rendering/CoinOffscreenGLCanvas.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoPointSet.h>
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

struct CallbackCounts {
  int pre = 0;
  int post = 0;
};

void preRender(void * data, SoRenderManager *)
{
  ++static_cast<CallbackCounts *>(data)->pre;
}

void postRender(void * data, SoRenderManager *)
{
  ++static_cast<CallbackCounts *>(data)->post;
}

int countNonBlack(CoinOffscreenGLCanvas & canvas)
{
  std::vector<uint8_t> pixels(32 * 32 * 4, 0);
  canvas.readPixels(pixels.data(), SbVec2s(32, 32), 32, 4);
  int count = 0;
  for (int i = 0; i < 32 * 32; ++i) {
    if (pixels[i * 4] > 5 || pixels[i * 4 + 1] > 5 || pixels[i * 4 + 2] > 5) {
      ++count;
    }
  }
  return count;
}

}

int
runTest()
{
  set_environment("COIN_EGL", "1");
  set_environment("EGL_PLATFORM", "surfaceless");
  set_environment("COIN_EGL_CORE_PROFILE", "1");

  CoinOffscreenGLCanvas canvas;
  canvas.setWantedSize(SbVec2s(32, 32));
  if (canvas.activateGLContext() == 0) {
    return skip("core EGL offscreen context is unavailable");
  }

  SoSeparator * cubeRoot = new SoSeparator;
  cubeRoot->addChild(new SoCube);
  cubeRoot->ref();

  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->position.setValue(0.0f, 0.0f, 5.0f);
  camera->pointAt(SbVec3f(0.0f, 0.0f, 0.0f));
  camera->ref();

  // The manager's camera bridge must feed real matrices into traversal.
  SoIRRenderAction cameraAction(SbViewportRegion(SbVec2s(32, 32)));
  cameraAction.setCamera(camera);
  cameraAction.apply(cubeRoot);
  if (SoViewingMatrixElement::get(cameraAction.getState()) == SbMatrix::identity()
      || SoProjectionMatrixElement::get(cameraAction.getState()) == SbMatrix::identity()) {
    std::cerr << "FAIL: transformed camera did not reach retained traversal state" << std::endl;
    camera->unref();
    cubeRoot->unref();
    canvas.deactivateGLContext();
    return 1;
  }

  int result = 0;
  {
    SoRenderManager manager;
    manager.setViewportRegion(SbViewportRegion(SbVec2s(32, 32)));
    manager.setSceneGraph(cubeRoot);
    manager.setCamera(camera);
    manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);

    CallbackCounts callbacks;
    manager.addPreRenderCallback(preRender, &callbacks);
    manager.addPostRenderCallback(postRender, &callbacks);
    manager.render(TRUE, TRUE);
    if (callbacks.pre != 1 || callbacks.post != 1) {
      std::cerr << "FAIL: DrawList manager callbacks were not paired exactly once" << std::endl;
      result = 1;
    }
    if (countNonBlack(canvas) == 0) {
      std::cerr << "FAIL: transformed-camera manager render produced no pixels" << std::endl;
      result = 1;
    }

    SoSeparator * dprRoot = new SoSeparator;
    dprRoot->ref();
    SoSeparator * lineGroup = new SoSeparator;
    SoDrawStyle * lineStyle = new SoDrawStyle;
    lineStyle->style = SoDrawStyle::LINES;
    lineStyle->lineWidth = 2.0f;
    SoCoordinate3 * lineCoords = new SoCoordinate3;
    lineCoords->point.set1Value(0, SbVec3f(-0.8f, -0.3f, 0.0f));
    lineCoords->point.set1Value(1, SbVec3f(0.8f, -0.3f, 0.0f));
    SoLineSet * line = new SoLineSet;
    line->numVertices.set1Value(0, 2);
    lineGroup->addChild(lineStyle);
    lineGroup->addChild(lineCoords);
    lineGroup->addChild(line);

    SoSeparator * pointGroup = new SoSeparator;
    SoDrawStyle * pointStyle = new SoDrawStyle;
    pointStyle->style = SoDrawStyle::POINTS;
    pointStyle->pointSize = 2.0f;
    SoCoordinate3 * pointCoords = new SoCoordinate3;
    pointCoords->point.set1Value(0, SbVec3f(0.0f, 0.35f, 0.0f));
    SoPointSet * points = new SoPointSet;
    points->numPoints = 1;
    pointGroup->addChild(pointStyle);
    pointGroup->addChild(pointCoords);
    pointGroup->addChild(points);
    dprRoot->addChild(lineGroup);
    dprRoot->addChild(pointGroup);

    manager.setCamera(NULL);
    manager.setSceneGraph(dprRoot);
    manager.setDevicePixelRatio(1.0f);
    manager.render(TRUE, TRUE);
    const int normalCoverage = countNonBlack(canvas);
    manager.setDevicePixelRatio(2.0f);
    manager.render(TRUE, TRUE);
    const int highDprCoverage = countNonBlack(canvas);
    if (highDprCoverage <= normalCoverage) {
      std::cerr << "FAIL: DPR did not widen retained points/lines" << std::endl;
      result = 1;
    }
    dprRoot->unref();
  }

  camera->unref();
  cubeRoot->unref();
  canvas.deactivateGLContext();
  return result;
}

int
main()
{
  SoDB::init();
  const int result = runTest();
  SoDB::finish();
  return result;
}
