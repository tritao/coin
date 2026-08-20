#include "support/GLTestContext.h"

#include <Inventor/SoDB.h>
#include <Inventor/C/glue/gl.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/system/gl.h>
#include <Inventor/lists/SoPickedPointList.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoTranslation.h>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

namespace {

int skip(const char * reason)
{
  std::cout << "SKIP: " << reason << std::endl;
  return 77;
}

bool retainedGLBaselineIsValid(const GLTestContext & context)
{
  const int major = context.majorVersion();
  const int minor = context.minorVersion();

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

void countTraversal(void * data, SoAction *)
{
  ++*static_cast<int *>(data);
}

int countNonBlack(GLTestContext & context)
{
  const std::vector<uint8_t> pixels = context.readPixels();
  int count = 0;
  for (int i = 0; i < 32 * 32; ++i) {
    if (pixels[i * 4] > 5 || pixels[i * 4 + 1] > 5 || pixels[i * 4 + 2] > 5) {
      ++count;
    }
  }
  return count;
}

int countNonBlackRow(GLTestContext & context, const int y)
{
  const std::vector<uint8_t> pixels = context.readPixels();
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

PixelRGB centerPixel(GLTestContext & context)
{
  const std::vector<uint8_t> pixels = context.readPixels();
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

PixelRGB renderTransparentOrder(GLTestContext & context,
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

  const PixelRGB result = centerPixel(context);
  root->unref();
  return result;
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
  GLTestContext context;
  if (!context.initialize(config)) {
    return skip("core GLFW OpenGL context is unavailable");
  }
  if (!retainedGLBaselineIsValid(context)) {
    std::cerr << "FAIL: manager test requires OpenGL 3.3 / GLSL 330" << std::endl;
    return 1;
  }

  SbViewportRegion testViewport(SbVec2s(32, 32));
  testViewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(32, 32));

  int traversalCount = 0;
  SoSeparator * cubeRoot = new SoSeparator;
  SoCallback * traversalCounter = new SoCallback;
  traversalCounter->setCallback(countTraversal, &traversalCount);
  SoTranslation * cubeTranslation = new SoTranslation;
  cubeTranslation->translation.setValue(0.0f, 0.0f, -3.0f);
  SoMaterial * cubeMaterial = new SoMaterial;
  SoSeparator * cubeOccurrence = new SoSeparator;
  cubeRoot->addChild(traversalCounter);
  cubeOccurrence->addChild(cubeTranslation);
  cubeOccurrence->addChild(cubeMaterial);
  cubeOccurrence->addChild(new SoCube);
  cubeRoot->addChild(cubeOccurrence);
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
      return 1;
    }
  }

  // A scene root that owns its camera must begin traversal from identity so
  // state before the camera node is not transformed by the manager camera.
  // This is explicit rather than inferred by searching the root graph.
  SoSeparator * cameraRoot = new SoSeparator;
  SoPointLight * sceneLight = new SoPointLight;
  sceneLight->location.setValue(1.0f, 0.0f, 0.0f);
  SoPerspectiveCamera * sceneCamera = new SoPerspectiveCamera;
  sceneCamera->position.setValue(5.0f, 0.0f, 5.0f);
  sceneCamera->pointAt(SbVec3f(0.0f, 0.0f, 0.0f));
  cameraRoot->addChild(sceneLight);
  cameraRoot->addChild(sceneCamera);
  cameraRoot->addChild(new SoCube);
  cameraRoot->ref();
  {
    SoIRRenderAction sceneCameraAction(testViewport);
    sceneCameraAction.setCamera(camera);
    sceneCameraAction.setCameraPolicy(
      SoIRRenderAction::CameraPolicy::CAMERA_IN_ROOT);
    sceneCameraAction.apply(cameraRoot);
    const SoDrawList & sceneCameraDrawList = sceneCameraAction.getDrawList();
    SoIRRenderAction configuredCameraAction(testViewport);
    configuredCameraAction.setCamera(camera);
    configuredCameraAction.apply(cameraRoot);
    const SoDrawList & configuredCameraDrawList = configuredCameraAction.getDrawList();
    if (sceneCameraDrawList.getNumCommands() == 0 ||
        configuredCameraDrawList.getNumCommands() == 0) {
      std::cerr << "FAIL: explicit scene-camera policy did not use the root camera" << std::endl;
      cameraRoot->unref();
      camera->unref();
      cubeRoot->unref();
      return 1;
    }
    const SoRenderCommand & sceneCameraCommand = sceneCameraDrawList.getCommand(0);
    const SoRenderCommand & configuredCameraCommand = configuredCameraDrawList.getCommand(0);
    const SoLightingData * sceneLighting = sceneCameraDrawList.getLighting(
      sceneCameraCommand.lightingHandle);
    const SoLightingData * configuredLighting = configuredCameraDrawList.getLighting(
      configuredCameraCommand.lightingHandle);
    if (sceneCameraCommand.viewMatrix == SbMatrix::identity() ||
        !sceneLighting || !configuredLighting || sceneLighting->lights.empty() ||
        configuredLighting->lights.empty() ||
        (sceneLighting->lights[0].position - configuredLighting->lights[0].position).length()
          < 1e-4f) {
      std::cerr << "FAIL: explicit scene-camera policy did not preserve pre-camera state" << std::endl;
      cameraRoot->unref();
      camera->unref();
      cubeRoot->unref();
      return 1;
    }
  }
  cameraRoot->unref();

  int result = 0;
  // A shared state node should update every dependent retained command in one
  // incremental render, including commands below nested ancestor paths.
  SoSeparator * sharedGeometryRoot = new SoSeparator;
  SoSeparator * sharedGeometryContainer = new SoSeparator;
  SoSeparator * sharedGeometryBranch = new SoSeparator;
  SoCoordinate3 * sharedCoordinates = new SoCoordinate3;
  const SbVec3f initialTriangle[] = {
    SbVec3f(-0.4f, -0.4f, 0.0f), SbVec3f(0.4f, -0.4f, 0.0f),
    SbVec3f(0.0f, 0.4f, 0.0f)
  };
  sharedCoordinates->point.setValues(0, 3, initialTriangle);
  sharedGeometryBranch->addChild(sharedCoordinates);
  SoFaceSet * sharedFace = new SoFaceSet;
  sharedFace->numVertices.set1Value(0, 3);
  for (int i = 0; i < 3; ++i) {
    SoSeparator * occurrence = new SoSeparator;
    SoTranslation * offset = new SoTranslation;
    offset->translation.setValue(
      0.6f * static_cast<float>(i - 1), 0.0f, 0.0f);
    occurrence->addChild(offset);
    occurrence->addChild(sharedFace);
    sharedGeometryBranch->addChild(occurrence);
  }
  sharedGeometryContainer->addChild(sharedGeometryBranch);
  sharedGeometryRoot->addChild(sharedGeometryContainer);
  sharedGeometryRoot->ref();
  {
    SoRenderManager sharedManager;
    sharedManager.setViewportRegion(testViewport);
    sharedManager.setSceneGraph(sharedGeometryRoot);
    sharedManager.setCamera(camera);
    sharedManager.setLightingMode(SoRenderManager::UNLIT);
    sharedManager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
    sharedManager.render(TRUE, TRUE);
    const std::vector<uint8_t> initialPixels = context.readPixels();
    sharedCoordinates->point.set1Value(0, SbVec3f(-0.3f, -0.4f, 0.0f));
    sharedManager.render(TRUE, TRUE);
    const SoRenderStatistics sharedStatistics =
      sharedManager.getRenderStatistics();
    const std::vector<uint8_t> incrementalPixels = context.readPixels();
    size_t differingFromInitial = 0;
    for (size_t i = 0; i < incrementalPixels.size(); ++i) {
      if (incrementalPixels[i] != initialPixels[i]) ++differingFromInitial;
    }
    if (sharedStatistics.drawListRebuilds != 0 ||
        sharedStatistics.incrementalCommandUpdates != 3 ||
        differingFromInitial == 0) {
      std::cerr << "FAIL: shared state did not update all retained geometries"
                << " (rebuilds=" << sharedStatistics.drawListRebuilds
                << ", updated="
                << sharedStatistics.incrementalCommandUpdates
                << ", changed bytes=" << differingFromInitial << ")"
                << std::endl;
      result = 1;
    }
    sharedManager.releaseRenderBackendResources();
    sharedManager.setCamera(NULL);
    sharedManager.setSceneGraph(NULL);
  }
  sharedGeometryRoot->unref();

  {
    SoRenderManager manager;
    manager.setViewportRegion(testViewport);
    manager.setSceneGraph(cubeRoot);
    manager.setCamera(camera);
    manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);

    if (!manager.isRenderPipelineAvailable(SoRenderManager::RenderPipeline::DRAW_LIST)) {
      std::cerr << "FAIL: DrawList was unavailable in a valid core context" << std::endl;
      result = 1;
    }

    CallbackCounts callbacks;
    manager.addPreRenderCallback(preRender, &callbacks);
    manager.addPostRenderCallback(postRender, &callbacks);
    manager.render(TRUE, TRUE);
    const int traversalCountAfterFirstRender = traversalCount;
    const SoRenderManager::RenderResult & renderResult = manager.getLastRenderResult();
    if (!renderResult.rendered ||
        renderResult.requestedPipeline != SoRenderManager::RenderPipeline::DRAW_LIST ||
        renderResult.usedPipeline != SoRenderManager::RenderPipeline::DRAW_LIST ||
        renderResult.fallbackReason !=
          SoRenderManager::RenderResult::FallbackReason::NONE) {
      std::cerr << "FAIL: DrawList render result did not report the executed pipeline"
                << std::endl;
      result = 1;
    }
    if (callbacks.pre != 1 || callbacks.post != 1) {
      std::cerr << "FAIL: DrawList manager callbacks were not paired exactly once" << std::endl;
      result = 1;
    }
    manager.render(TRUE, TRUE);
    if (traversalCount != traversalCountAfterFirstRender) {
      std::cerr << "FAIL: unchanged DrawList render repeated scene traversal"
                << std::endl;
      result = 1;
    }
    cubeTranslation->translation.setValue(0.1f, 0.0f, -3.0f);
    manager.render(TRUE, TRUE);
    const SoRenderStatistics transformStatistics = manager.getRenderStatistics();
    if (traversalCount != traversalCountAfterFirstRender ||
        transformStatistics.drawListRebuilds != 0 ||
        transformStatistics.incrementalCommandUpdates != 1) {
      std::cerr << "FAIL: isolated translation did not update its retained command"
                << std::endl;
      result = 1;
    }
    cubeTranslation->translation.setValue(0.2f, 0.0f, -3.0f);
    cubeTranslation->translation.setValue(0.3f, 0.0f, -3.0f);
    manager.render(TRUE, TRUE);
    const SoRenderStatistics repeatedTransformStatistics =
      manager.getRenderStatistics();
    if (traversalCount != traversalCountAfterFirstRender + 1 ||
        repeatedTransformStatistics.drawListRebuilds != 1 ||
        repeatedTransformStatistics.incrementalCommandUpdates != 0) {
      std::cerr << "FAIL: multiple notifications did not use a full rebuild"
                << std::endl;
      result = 1;
    }
    cubeMaterial->diffuseColor.setValue(0.2f, 0.6f, 0.8f);
    manager.render(TRUE, TRUE);
    const SoRenderStatistics materialStatistics = manager.getRenderStatistics();
    if (traversalCount != traversalCountAfterFirstRender + 1 ||
        materialStatistics.drawListRebuilds != 0 ||
        materialStatistics.incrementalCommandUpdates != 1) {
      std::cerr << "FAIL: isolated diffuse color did not update its retained command"
                << std::endl;
      result = 1;
    }
    const PixelRGB incrementalMaterialPixel = centerPixel(context);
    manager.invalidateDrawList();
    manager.render(TRUE, TRUE);
    const PixelRGB rebuiltMaterialPixel = centerPixel(context);
    if (incrementalMaterialPixel.red != rebuiltMaterialPixel.red ||
        incrementalMaterialPixel.green != rebuiltMaterialPixel.green ||
        incrementalMaterialPixel.blue != rebuiltMaterialPixel.blue) {
      std::cerr << "FAIL: incremental diffuse color differs from full rebuild"
                << std::endl;
      result = 1;
    }
    cubeMaterial->transparency.setValue(0.2f);
    manager.render(TRUE, TRUE);
    const SoRenderStatistics transparencyStatistics =
      manager.getRenderStatistics();
    const std::vector<uint8_t> incrementalTransparencyPixels =
      context.readPixels();
    manager.invalidateDrawList();
    manager.render(TRUE, TRUE);
    const std::vector<uint8_t> rebuiltTransparencyPixels = context.readPixels();
    if (traversalCount != traversalCountAfterFirstRender + 3 ||
        transparencyStatistics.drawListRebuilds != 0 ||
        transparencyStatistics.incrementalCommandUpdates != 1 ||
        incrementalTransparencyPixels != rebuiltTransparencyPixels) {
      std::cerr << "FAIL: incremental transparency differs from full rebuild"
                << std::endl;
      result = 1;
    }
    cubeMaterial->transparency.setValue(0.0f);
    manager.render(TRUE, TRUE);
    const SoRenderStatistics opaqueStatistics = manager.getRenderStatistics();
    const std::vector<uint8_t> incrementalOpaquePixels = context.readPixels();
    manager.invalidateDrawList();
    manager.render(TRUE, TRUE);
    const std::vector<uint8_t> rebuiltOpaquePixels = context.readPixels();
    if (traversalCount != traversalCountAfterFirstRender + 4 ||
        opaqueStatistics.drawListRebuilds != 0 ||
        opaqueStatistics.incrementalCommandUpdates != 1 ||
        incrementalOpaquePixels != rebuiltOpaquePixels) {
      std::cerr << "FAIL: incremental opaque transition differs from full rebuild"
                << std::endl;
      result = 1;
    }
    cubeMaterial->shininess.setValue(0.6f);
    manager.render(TRUE, TRUE);
    const SoRenderStatistics shininessStatistics = manager.getRenderStatistics();
    if (traversalCount != traversalCountAfterFirstRender + 4 ||
        shininessStatistics.drawListRebuilds != 0 ||
        shininessStatistics.incrementalCommandUpdates != 1) {
      std::cerr << "FAIL: isolated shininess did not update retained material"
                << std::endl;
      result = 1;
    }
    cubeRoot->touch();
    manager.render(TRUE, TRUE);
    if (traversalCount != traversalCountAfterFirstRender + 5) {
      std::cerr << "FAIL: changed scene did not invalidate cached DrawList"
                << std::endl;
      result = 1;
    }
    if (callbacks.pre != 12 || callbacks.post != 12) {
      std::cerr << "FAIL: cached DrawList renders skipped manager callbacks"
                << std::endl;
      result = 1;
    }
    if (countNonBlack(context) == 0) {
      std::cerr << "FAIL: transformed-camera manager render produced no pixels" << std::endl;
      result = 1;
    }

    SoPickedPoint * closest = NULL;
    if (!manager.pickClosest(16, 16, 0, closest) || !closest ||
        closest->getPath()->getTail()->getTypeId() !=
          SoCube::getClassTypeId() ||
        !std::isfinite(closest->getPoint()[0]) ||
        !std::isfinite(closest->getPoint()[1]) ||
        !std::isfinite(closest->getPoint()[2])) {
      std::cerr << "FAIL: manager did not resolve retained identity into "
                   "a SoPickedPoint" << std::endl;
      result = 1;
    }
    delete closest;

    SoAsyncPickRequest hoverRequest;
    if (!manager.requestPickClosestAsync(16, 16, 0, hoverRequest)) {
      std::cerr << "FAIL: manager rejected asynchronous hover pick"
                << std::endl;
      result = 1;
    }
    else {
      SoPickedPoint * hover = NULL;
      SoAsyncPickStatus hoverStatus =
        manager.pollPickClosestAsync(hoverRequest, hover);
      if (hoverStatus == SoAsyncPickStatus::PENDING) {
        glFinish();
        hoverStatus = manager.pollPickClosestAsync(hoverRequest, hover);
      }
      if (hoverStatus != SoAsyncPickStatus::HIT || !hover ||
          hover->getPath()->getTail()->getTypeId() !=
            SoCube::getClassTypeId()) {
        std::cerr << "FAIL: manager asynchronous hover pick was incorrect"
                  << std::endl;
        result = 1;
      }
      delete hover;
    }

    SoAsyncPickRequest identityRequest;
    if (!manager.requestPickIdentityAsync(16, 16, 0, identityRequest) ||
        identityRequest.mode != SoPickReadbackMode::ID_ONLY) {
      std::cerr << "FAIL: manager rejected asynchronous pick identity"
                << std::endl;
      result = 1;
    }
    else {
      SoPickIdentity identity;
      SoAsyncPickStatus identityStatus =
        manager.pollPickIdentityAsync(identityRequest, identity);
      if (identityStatus == SoAsyncPickStatus::PENDING) {
        glFinish();
        identityStatus = manager.pollPickIdentityAsync(identityRequest,
                                                        identity);
      }
      if (identityStatus != SoAsyncPickStatus::HIT ||
          identity.generation == 0 || identity.commandIndex < 0) {
        std::cerr << "FAIL: manager asynchronous identity was incorrect"
                  << std::endl;
        result = 1;
      }
    }

    SoPickedPointList stack;
    if (!manager.pickDepthStack(16, 16, 0, 8, stack) ||
        stack.getLength() == 0 ||
        stack[0]->getPath()->getTail()->getTypeId() !=
          SoCube::getClassTypeId()) {
      std::cerr << "FAIL: manager depth-stack query did not return scene hits"
                << std::endl;
      result = 1;
    }

    manager.setRenderMode(SoRenderManager::WIREFRAME);
    manager.render(TRUE, TRUE);
    if (countNonBlack(context) == 0) {
      std::cerr << "FAIL: DrawList manager wireframe mode produced no pixels" << std::endl;
      result = 1;
    }
    manager.setRenderMode(SoRenderManager::POINTS);
    manager.render(TRUE, TRUE);
    if (countNonBlack(context) == 0) {
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
    const int normalCoverage = countNonBlack(context);
    manager.setDevicePixelRatio(2.0f);
    manager.render(TRUE, TRUE);
    const int highDprCoverage = countNonBlack(context);
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

  // Coordinate changes regenerate all compatible commands in their retained
  // branch as one transaction.
  {
    const SbVec3f triangle[] = {
      SbVec3f(-0.8f, -0.8f, 0.0f), SbVec3f(0.8f, -0.8f, 0.0f),
      SbVec3f(0.0f, 0.8f, 0.0f)
    };
    SoSeparator * geometryRoot = new SoSeparator;
    geometryRoot->ref();
    SoSeparator * geometryOccurrence = new SoSeparator;
    SoCoordinate3 * coordinates = new SoCoordinate3;
    coordinates->point.setValues(0, 3, triangle);
    geometryOccurrence->addChild(coordinates);
    geometryOccurrence->addChild(new SoFaceSet);
    geometryRoot->addChild(geometryOccurrence);

    SoRenderManager geometryManager;
    geometryManager.setViewportRegion(testViewport);
    geometryManager.setSceneGraph(geometryRoot);
    geometryManager.setCamera(camera);
    geometryManager.setRenderPipeline(
      SoRenderManager::RenderPipeline::DRAW_LIST);
    geometryManager.render(TRUE, TRUE);
    coordinates->point.set1Value(0, SbVec3f(-0.6f, -0.8f, 0.0f));
    geometryManager.render(TRUE, TRUE);
    const SoRenderStatistics firstGeometryStatistics =
      geometryManager.getRenderStatistics();
    coordinates->point.set1Value(0, SbVec3f(-0.5f, -0.8f, 0.0f));
    geometryManager.render(TRUE, TRUE);
    const SoRenderStatistics geometryStatistics =
      geometryManager.getRenderStatistics();
    const std::vector<uint8_t> incrementalGeometryPixels = context.readPixels();
    geometryManager.invalidateDrawList();
    geometryManager.render(TRUE, TRUE);
    const std::vector<uint8_t> rebuiltGeometryPixels = context.readPixels();
    if (firstGeometryStatistics.drawListRebuilds != 0 ||
        firstGeometryStatistics.incrementalCommandUpdates != 1 ||
        geometryStatistics.drawListRebuilds != 0 ||
        geometryStatistics.incrementalCommandUpdates != 1 ||
        incrementalGeometryPixels != rebuiltGeometryPixels) {
      std::cerr << "FAIL: incremental geometry differs from full rebuild"
                << " (rebuilds=" << geometryStatistics.drawListRebuilds
                << ", updates=" << geometryStatistics.incrementalCommandUpdates
                << ')' << std::endl;
      result = 1;
    }

    geometryOccurrence->addChild(new SoFaceSet);
    geometryManager.render(TRUE, TRUE);
    coordinates->point.set1Value(0, SbVec3f(-0.7f, -0.8f, 0.0f));
    geometryManager.render(TRUE, TRUE);
    const SoRenderStatistics sharedGeometryStatistics =
      geometryManager.getRenderStatistics();
    const std::vector<uint8_t> incrementalSharedPixels = context.readPixels();
    geometryManager.invalidateDrawList();
    geometryManager.render(TRUE, TRUE);
    const std::vector<uint8_t> rebuiltSharedPixels = context.readPixels();
    if (sharedGeometryStatistics.drawListRebuilds != 0 ||
        sharedGeometryStatistics.incrementalCommandUpdates != 2 ||
        incrementalSharedPixels != rebuiltSharedPixels) {
      std::cerr << "FAIL: shared coordinate commands were not regenerated"
                << std::endl;
      result = 1;
    }
    geometryManager.setSceneGraph(NULL);
    geometryRoot->unref();
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
  const int insideRows = countNonBlackRow(context, croppedOrigin[1] + croppedSize[1] / 2);
  const int outsideTopRows = countNonBlackRow(context, 0);
  const int outsideBottomRows = countNonBlackRow(context, 31);
  if (insideRows == 0 || outsideTopRows != 0 || outsideBottomRows != 0) {
    std::cerr << "FAIL: manager did not apply the camera's cropped viewport" << std::endl;
    result = 1;
  }
  croppedRoot->unref();

  camera->viewportMapping = SoCamera::ADJUST_CAMERA;
  camera->aspectRatio = 1.0f;

  const PixelRGB nearFirst = renderTransparentOrder(context, camera,
                                                    testViewport, true);
  const PixelRGB farFirst = renderTransparentOrder(context, camera,
                                                   testViewport, false);
  if (std::abs(nearFirst.red - farFirst.red) > 3 ||
      std::abs(nearFirst.green - farFirst.green) > 3 ||
      std::abs(nearFirst.blue - farFirst.blue) > 3) {
    std::cerr << "FAIL: manager transparent scheduling depends on insertion order" << std::endl;
    result = 1;
  }

  // Reinitialization and replacement must not delete GL names owned by the
  // new context while disposing of resources created in the old context.
  GLTestContext secondContext;
  SoRenderManager * contextManager = new SoRenderManager;
  contextManager->setViewportRegion(testViewport);
  contextManager->setSceneGraph(cubeRoot);
  contextManager->setCamera(camera);
  contextManager->setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
  contextManager->render(TRUE, TRUE);
  if (!secondContext.initialize(config)) {
    delete contextManager;
    context.makeCurrent();
    std::cerr << "FAIL: second GLFW context was unavailable for replacement test" << std::endl;
    result = 1;
  }
  else {
    SbBool secondContextActive = TRUE;
    const cc_glglue * secondGlue = secondContext.glue();
    GLuint sentinelBuffer = 0;
    cc_glglue_glGenBuffers(secondGlue, 1, &sentinelBuffer);
    cc_glglue_glBindBuffer(secondGlue, GL_ARRAY_BUFFER, sentinelBuffer);
    cc_glglue_glBufferData(secondGlue, GL_ARRAY_BUFFER, 16, NULL,
                           GL_STATIC_DRAW);
    cc_glglue_glBindBuffer(secondGlue, GL_ARRAY_BUFFER, 0);

    // The manager still owns resources from the first context here.
    contextManager->reinitialize();
    if (!cc_glglue_glIsBuffer(secondGlue, sentinelBuffer)) {
      std::cerr << "FAIL: reinitialize deleted a new-context GL buffer" << std::endl;
      result = 1;
    }
    delete contextManager;

    // Recreate a manager in the first context, then exercise replacement in
    // the second context without first calling reinitialize().
    secondContextActive = FALSE;
    if (!context.makeCurrent()) {
      std::cerr << "FAIL: original GLFW context could not be restored for replacement test" << std::endl;
      result = 1;
    }
    else {
      SoRenderManager * replacementManager = new SoRenderManager;
      replacementManager->setViewportRegion(testViewport);
      replacementManager->setSceneGraph(cubeRoot);
      replacementManager->setCamera(camera);
      replacementManager->setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
      replacementManager->render(TRUE, TRUE);

      if (!secondContext.makeCurrent()) {
        std::cerr << "FAIL: second GLFW context could not be restored for replacement test" << std::endl;
        result = 1;
      }
      else {
        secondContextActive = TRUE;
        replacementManager->render(TRUE, TRUE);
        if (!cc_glglue_glIsBuffer(secondGlue, sentinelBuffer)) {
          std::cerr << "FAIL: context replacement deleted a new-context GL buffer" << std::endl;
          result = 1;
        }
      }
      delete replacementManager;
    }
    if (secondContextActive) {
      cc_glglue_glDeleteBuffers(secondGlue, 1, &sentinelBuffer);
    }
    secondContextActive = FALSE;
    if (!context.makeCurrent()) {
      std::cerr << "FAIL: original GLFW context could not be restored" << std::endl;
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
  SoAsyncPickRequest lostContextRequest;
  if (!lostContextManager->requestPickClosestAsync(
        16, 16, 0, lostContextRequest)) {
    std::cerr << "FAIL: lost-context setup could not queue async pick"
              << std::endl;
    result = 1;
  }
  context.shutdown();
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
