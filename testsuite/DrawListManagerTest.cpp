#include "rendering/CoinOffscreenGLCanvas.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/system/gl.h>
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
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoTranslation.h>

#include <cmath>
#include <cstdio>
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

bool retainedGLBaselineIsValid()
{
  GLint major = 0;
  GLint minor = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &major);
  glGetIntegerv(GL_MINOR_VERSION, &minor);

  const char * shadingLanguage = reinterpret_cast<const char *>(
    glGetString(GL_SHADING_LANGUAGE_VERSION));
  int shadingMajor = 0;
  int shadingMinor = 0;
  const bool parsedShadingLanguage =
    shadingLanguage &&
    std::sscanf(shadingLanguage, "%d.%d", &shadingMajor, &shadingMinor) == 2;

  const bool gl33 = major > 3 || (major == 3 && minor >= 3);
  const bool glsl330 = parsedShadingLanguage &&
    (shadingMajor > 3 ||
     (shadingMajor == 3 && shadingMinor >= 30));
  return gl33 && glsl330;
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

int countNonBlackRow(CoinOffscreenGLCanvas & canvas, const int y)
{
  std::vector<uint8_t> pixels(32 * 32 * 4, 0);
  canvas.readPixels(pixels.data(), SbVec2s(32, 32), 32, 4);
  int count = 0;
  for (int x = 0; x < 32; ++x) {
    const size_t offset = static_cast<size_t>(y * 32 + x) * 4;
    if (pixels[offset] > 5 || pixels[offset + 1] > 5 || pixels[offset + 2] > 5) {
      ++count;
    }
  }
  return count;
}

struct PixelRGB {
  int red;
  int green;
  int blue;
};

PixelRGB centerPixel(CoinOffscreenGLCanvas & canvas)
{
  std::vector<uint8_t> pixels(32 * 32 * 4, 0);
  canvas.readPixels(pixels.data(), SbVec2s(32, 32), 32, 4);
  const size_t offset = static_cast<size_t>(16 * 32 + 16) * 4;
  return { pixels[offset], pixels[offset + 1], pixels[offset + 2] };
}

void addTransparentCube(SoSeparator * root, const float z,
                        const SbColor & color)
{
  SoSeparator * group = new SoSeparator;
  SoTranslation * translation = new SoTranslation;
  translation->translation.setValue(0.0f, 0.0f, z);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor = color;
  material->transparency = 0.5f;
  group->addChild(translation);
  group->addChild(material);
  group->addChild(new SoCube);
  root->addChild(group);
}

PixelRGB renderTransparentOrder(CoinOffscreenGLCanvas & canvas,
                                SoCamera * camera,
                                const SbViewportRegion & viewport,
                                const bool nearFirst)
{
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoLightModel * lightModel = new SoLightModel;
  lightModel->model = SoLightModel::BASE_COLOR;
  root->addChild(lightModel);
  if (nearFirst) {
    addTransparentCube(root, -1.0f, SbColor(1.0f, 0.0f, 0.0f));
    addTransparentCube(root, -3.0f, SbColor(0.0f, 0.0f, 1.0f));
  }
  else {
    addTransparentCube(root, -3.0f, SbColor(0.0f, 0.0f, 1.0f));
    addTransparentCube(root, -1.0f, SbColor(1.0f, 0.0f, 0.0f));
  }

  SoRenderManager manager;
  manager.setViewportRegion(viewport);
  manager.setSceneGraph(root);
  manager.setCamera(camera);
  manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
  manager.render(TRUE, TRUE);

  const PixelRGB result = centerPixel(canvas);
  root->unref();
  return result;
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
  if (!retainedGLBaselineIsValid()) {
    std::cerr << "FAIL: manager test requires OpenGL 3.3 / GLSL 330" << std::endl;
    canvas.deactivateGLContext();
    return 1;
  }

  SbViewportRegion testViewport(SbVec2s(32, 32));
  testViewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(32, 32));

  SoSeparator * cubeRoot = new SoSeparator;
  SoTranslation * cubeTranslation = new SoTranslation;
  cubeTranslation->translation.setValue(0.0f, 0.0f, -3.0f);
  cubeRoot->addChild(cubeTranslation);
  cubeRoot->addChild(new SoCube);
  cubeRoot->ref();

  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->position.setValue(0.0f, 0.0f, 5.0f);
  camera->pointAt(SbVec3f(0.0f, 0.0f, 0.0f));
  camera->ref();

  // The manager's camera bridge must feed real matrices into traversal.
  {
    SoIRRenderAction cameraAction(testViewport);
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

    manager.setRenderMode(SoRenderManager::WIREFRAME);
    manager.render(TRUE, TRUE);
    if (countNonBlack(canvas) == 0) {
      std::cerr << "FAIL: DrawList manager wireframe mode produced no pixels" << std::endl;
      result = 1;
    }
    manager.setRenderMode(SoRenderManager::POINTS);
    manager.render(TRUE, TRUE);
    if (countNonBlack(canvas) == 0) {
      std::cerr << "FAIL: DrawList manager points mode produced no pixels" << std::endl;
      result = 1;
    }
    manager.setRenderMode(SoRenderManager::AS_IS);

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

  camera->viewportMapping = SoCamera::CROP_VIEWPORT_NO_FRAME;
  camera->aspectRatio = 2.0f;
  SbViewportRegion croppedViewport;
  static_cast<SoCamera *>(camera)->getViewVolume(testViewport, croppedViewport);
  if (croppedViewport.getViewportSizePixels() ==
      testViewport.getViewportSizePixels()) {
    std::cerr << "FAIL: cropped camera did not produce an effective viewport" << std::endl;
    result = 1;
  }

  // The manager must forward that effective viewport to the backend. Make
  // the geometry fill the frame so rendering to the full viewport would
  // visibly paint the rows outside the cropped region.
  SoSeparator * croppedRoot = new SoSeparator;
  croppedRoot->ref();
  SoScale * croppedScale = new SoScale;
  croppedScale->scaleFactor.setValue(0.9f, 3.0f, 1.0f);
  SoTranslation * croppedTranslation = new SoTranslation;
  croppedTranslation->translation.setValue(0.0f, 0.0f, -3.0f);
  croppedRoot->addChild(croppedScale);
  croppedRoot->addChild(croppedTranslation);
  croppedRoot->addChild(new SoCube);

  SoRenderManager croppedManager;
  croppedManager.setViewportRegion(testViewport);
  croppedManager.setSceneGraph(croppedRoot);
  croppedManager.setCamera(camera);
  croppedManager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
  croppedManager.render(TRUE, TRUE);
  const SbVec2s croppedOrigin = croppedViewport.getViewportOriginPixels();
  const SbVec2s croppedSize = croppedViewport.getViewportSizePixels();
  const int insideRows = countNonBlackRow(canvas, croppedOrigin[1] + croppedSize[1] / 2);
  const int outsideTopRows = countNonBlackRow(canvas, 0);
  const int outsideBottomRows = countNonBlackRow(canvas, 31);
  if (insideRows == 0 || outsideTopRows != 0 || outsideBottomRows != 0) {
    std::cerr << "FAIL: manager did not apply the camera's cropped viewport" << std::endl;
    result = 1;
  }
  croppedRoot->unref();

  camera->viewportMapping = SoCamera::ADJUST_CAMERA;
  camera->aspectRatio = 1.0f;

  const PixelRGB nearFirst = renderTransparentOrder(canvas, camera,
                                                    testViewport, true);
  const PixelRGB farFirst = renderTransparentOrder(canvas, camera,
                                                   testViewport, false);
  if (std::abs(nearFirst.red - farFirst.red) > 3 ||
      std::abs(nearFirst.green - farFirst.green) > 3 ||
      std::abs(nearFirst.blue - farFirst.blue) > 3) {
    std::cerr << "FAIL: manager transparent scheduling depends on insertion order" << std::endl;
    result = 1;
  }

  // Reinitialization and replacement must not delete GL names owned by the
  // new context while disposing of resources created in the old context.
  CoinOffscreenGLCanvas secondCanvas;
  secondCanvas.setWantedSize(SbVec2s(32, 32));
  SoRenderManager * contextManager = new SoRenderManager;
  contextManager->setViewportRegion(testViewport);
  contextManager->setSceneGraph(cubeRoot);
  contextManager->setCamera(camera);
  contextManager->setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
  contextManager->render(TRUE, TRUE);
  canvas.deactivateGLContext();
  if (secondCanvas.activateGLContext() == 0) {
    delete contextManager;
    canvas.activateGLContext();
    std::cerr << "FAIL: second EGL context was unavailable for replacement test" << std::endl;
    result = 1;
  }
  else {
    SbBool secondContextActive = TRUE;
    GLuint sentinelBuffer = 0;
    glGenBuffers(1, &sentinelBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, sentinelBuffer);
    glBufferData(GL_ARRAY_BUFFER, 16, NULL, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // The manager still owns resources from the first context here.
    contextManager->reinitialize();
    if (!glIsBuffer(sentinelBuffer)) {
      std::cerr << "FAIL: reinitialize deleted a new-context GL buffer" << std::endl;
      result = 1;
    }
    delete contextManager;

    // Recreate a manager in the first context, then exercise replacement in
    // the second context without first calling reinitialize().
    secondCanvas.deactivateGLContext();
    secondContextActive = FALSE;
    if (canvas.activateGLContext() == 0) {
      std::cerr << "FAIL: original EGL context could not be restored for replacement test" << std::endl;
      result = 1;
    }
    else {
      SoRenderManager * replacementManager = new SoRenderManager;
      replacementManager->setViewportRegion(testViewport);
      replacementManager->setSceneGraph(cubeRoot);
      replacementManager->setCamera(camera);
      replacementManager->setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
      replacementManager->render(TRUE, TRUE);

      canvas.deactivateGLContext();
      if (secondCanvas.activateGLContext() == 0) {
        std::cerr << "FAIL: second EGL context could not be restored for replacement test" << std::endl;
        result = 1;
      }
      else {
        secondContextActive = TRUE;
        replacementManager->render(TRUE, TRUE);
        if (!glIsBuffer(sentinelBuffer)) {
          std::cerr << "FAIL: context replacement deleted a new-context GL buffer" << std::endl;
          result = 1;
        }
      }
      delete replacementManager;
    }
    if (secondContextActive) {
      glDeleteBuffers(1, &sentinelBuffer);
    }
    secondCanvas.deactivateGLContext();
    secondContextActive = FALSE;
    if (canvas.activateGLContext() == 0) {
      std::cerr << "FAIL: original EGL context could not be restored" << std::endl;
      result = 1;
    }
  }

  // Destroying a manager after its context has disappeared must not issue
  // GL deletes through the old context. The layer above the manager owns
  // eventual lost-context resource disposal.
  SoRenderManager * lostContextManager = new SoRenderManager;
  lostContextManager->setViewportRegion(SbViewportRegion(SbVec2s(32, 32)));
  lostContextManager->setSceneGraph(cubeRoot);
  lostContextManager->setCamera(camera);
  lostContextManager->setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
  lostContextManager->render(TRUE, TRUE);
  canvas.deactivateGLContext();
  delete lostContextManager;

  camera->unref();
  cubeRoot->unref();
  return result;
}

int
main()
{
  const int result = runTest();
  SoDB::finish();
  return result;
}
