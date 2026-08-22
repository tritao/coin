#include "support/GLTestContext.h"

#include <Inventor/SoDB.h>
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
#include <Inventor/nodes/SoSwitch.h>
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
  if (!retainedGLBaselineIsValid()) {
    std::cerr << "FAIL: manager test requires OpenGL 3.3 / GLSL 330" << std::endl;
    return 1;
  }

  SbViewportRegion testViewport(SbVec2s(32, 32));
  testViewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(32, 32));

  SoSeparator * cubeRoot = new SoSeparator;
  SoTranslation * cubeTranslation = new SoTranslation;
  cubeTranslation->translation.setValue(0.0f, 0.0f, -3.0f);
  SoTranslation * cubeNestedTranslation = new SoTranslation;
  SoMaterial * cubeMaterial = new SoMaterial;
  SoSeparator * cubeOccurrence = new SoSeparator;
  cubeOccurrence->addChild(cubeTranslation);
  cubeOccurrence->addChild(cubeNestedTranslation);
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
  {
    SoRenderManager manager;
    manager.setViewportRegion(testViewport);
    manager.setSceneGraph(cubeRoot);
    manager.setCamera(camera);
    manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);

    if (manager.isRenderPhaseTimingEnabled()) {
      std::cerr << "FAIL: render phase timing was enabled by default" << std::endl;
      result = 1;
    }
    manager.setRenderPhaseTimingEnabled(TRUE);

    if (!manager.isRenderPipelineAvailable(SoRenderManager::RenderPipeline::DRAW_LIST)) {
      std::cerr << "FAIL: DrawList was unavailable in a valid core context" << std::endl;
      result = 1;
    }

    CallbackCounts callbacks;
    manager.addPreRenderCallback(preRender, &callbacks);
    manager.addPostRenderCallback(postRender, &callbacks);
    manager.render(TRUE, TRUE);
    if (callbacks.pre != 1 || callbacks.post != 1) {
      std::cerr << "FAIL: DrawList manager callbacks were not paired exactly once" << std::endl;
      result = 1;
    }
    const SoRenderManager::RenderPhaseStatistics renderPhases =
      manager.getRenderPhaseStatistics();
    if (renderPhases.drawListConstructionNanoseconds == 0 ||
        renderPhases.planConstructionNanoseconds == 0 ||
        renderPhases.backendSubmissionNanoseconds == 0 ||
        renderPhases.backendFrameSetupNanoseconds == 0 ||
        renderPhases.backendResourcePreparationNanoseconds == 0 ||
        renderPhases.backendCommandExecutionNanoseconds == 0) {
      std::cerr << "FAIL: retained render phases were not measured" << std::endl;
      result = 1;
    }
    manager.render(TRUE, TRUE);
    const SoRenderManager::RenderPhaseStatistics reusedRenderPhases =
      manager.getRenderPhaseStatistics();
    if (reusedRenderPhases.drawListRebuilds != 0 ||
        reusedRenderPhases.drawListConstructionNanoseconds != 0) {
      std::cerr << "FAIL: unchanged retained frame was rebuilt" << std::endl;
      result = 1;
    }
    cubeTranslation->translation.setValue(0.1f, 0.0f, -3.0f);
    manager.render(TRUE, TRUE);
    const SoRenderManager::RenderPhaseStatistics transformPhases =
      manager.getRenderPhaseStatistics();
    if (transformPhases.drawListRebuilds != 0 ||
        transformPhases.incrementalCommandUpdates != 1 ||
        transformPhases.planConstructionNanoseconds != 0) {
      std::cerr << "FAIL: isolated translation did not patch its retained "
                   "command while preserving the render plan" << std::endl;
      result = 1;
    }
    cubeTranslation->translation.setValue(0.2f, 0.0f, -3.0f);
    cubeNestedTranslation->translation.setValue(0.1f, 0.0f, 0.0f);
    manager.render(TRUE, TRUE);
    const SoRenderManager::RenderPhaseStatistics transformBatchPhases =
      manager.getRenderPhaseStatistics();
    if (transformBatchPhases.drawListRebuilds != 0 ||
        transformBatchPhases.incrementalCommandUpdates != 1 ||
        transformBatchPhases.planConstructionNanoseconds != 0) {
      std::cerr << "FAIL: transform batch did not patch its unique retained "
                   "command" << std::endl;
      result = 1;
    }
    cubeMaterial->diffuseColor.setValue(0.8f, 0.2f, 0.1f);
    manager.render(TRUE, TRUE);
    const SoRenderManager::RenderPhaseStatistics materialPhases =
      manager.getRenderPhaseStatistics();
    if (materialPhases.drawListRebuilds != 0 ||
        materialPhases.incrementalCommandUpdates != 1 ||
        materialPhases.planConstructionNanoseconds != 0) {
      std::cerr << "FAIL: isolated diffuse color did not patch its retained "
                   "command while preserving the render plan" << std::endl;
      result = 1;
    }
    cubeMaterial->diffuseColor.setValue(0.7f, 0.3f, 0.2f);
    cubeMaterial->diffuseColor.setValue(0.6f, 0.4f, 0.3f);
    manager.render(TRUE, TRUE);
    const SoRenderManager::RenderPhaseStatistics materialBatchPhases =
      manager.getRenderPhaseStatistics();
    if (materialBatchPhases.drawListRebuilds != 0 ||
        materialBatchPhases.incrementalCommandUpdates != 1 ||
        materialBatchPhases.planConstructionNanoseconds != 0) {
      std::cerr << "FAIL: repeated material notifications were not coalesced"
                << std::endl;
      result = 1;
    }
    cubeNestedTranslation->translation.setValue(0.2f, 0.0f, 0.0f);
    cubeMaterial->diffuseColor.setValue(0.5f, 0.4f, 0.3f);
    manager.render(TRUE, TRUE);
    const SoRenderManager::RenderPhaseStatistics mixedBatchPhases =
      manager.getRenderPhaseStatistics();
    if (mixedBatchPhases.drawListRebuilds != 1 ||
        mixedBatchPhases.incrementalCommandUpdates != 0) {
      std::cerr << "FAIL: mixed state changes bypassed the safe rebuild path"
                << std::endl;
      result = 1;
    }

    const SbVec3f triangle[] = {
      SbVec3f(-0.8f, -0.8f, 0.0f),
      SbVec3f(0.8f, -0.8f, 0.0f),
      SbVec3f(0.0f, 0.8f, 0.0f)
    };
    SoSeparator * geometryRoot = new SoSeparator;
    SoSeparator * geometryOccurrence = new SoSeparator;
    SoCoordinate3 * coordinates = new SoCoordinate3;
    coordinates->point.setValues(0, 3, triangle);
    geometryOccurrence->addChild(coordinates);
    SoFaceSet * faceSet = new SoFaceSet;
    faceSet->numVertices.set1Value(0, 3);
    geometryOccurrence->addChild(faceSet);
    geometryRoot->addChild(geometryOccurrence);
    geometryRoot->ref();

    SoRenderManager geometryManager;
    geometryManager.setViewportRegion(testViewport);
    geometryManager.setSceneGraph(geometryRoot);
    geometryManager.setCamera(camera);
    geometryManager.setRenderPipeline(
      SoRenderManager::RenderPipeline::DRAW_LIST);
    geometryManager.setRenderPhaseTimingEnabled(TRUE);
    geometryManager.render(TRUE, TRUE);
    coordinates->point.set1Value(0, SbVec3f(-0.6f, -0.8f, 0.0f));
    geometryManager.render(TRUE, TRUE);
    const SoRenderManager::RenderPhaseStatistics geometryPhases =
      geometryManager.getRenderPhaseStatistics();
    const std::vector<uint8_t> incrementalGeometryPixels =
      context.readPixels();
    geometryManager.invalidateDrawList();
    geometryManager.render(TRUE, TRUE);
    const std::vector<uint8_t> rebuiltGeometryPixels = context.readPixels();
    size_t geometryPixelDifferences = 0;
    int maximumGeometryDifference = 0;
    for (size_t i = 0; i < incrementalGeometryPixels.size(); ++i) {
      const int difference = std::abs(
        static_cast<int>(incrementalGeometryPixels[i]) -
        static_cast<int>(rebuiltGeometryPixels[i]));
      if (difference != 0) ++geometryPixelDifferences;
      maximumGeometryDifference = std::max(maximumGeometryDifference,
                                           difference);
    }
    if (geometryPhases.drawListRebuilds != 0 ||
        geometryPhases.incrementalCommandUpdates != 1 ||
        incrementalGeometryPixels != rebuiltGeometryPixels) {
      std::cerr << "FAIL: unique geometry resource did not update "
                   "transactionally (rebuilds="
                << geometryPhases.drawListRebuilds << ", updates="
                << geometryPhases.incrementalCommandUpdates << ", parity="
                << (incrementalGeometryPixels == rebuiltGeometryPixels)
                << ", differing-bytes=" << geometryPixelDifferences
                << ", max-difference=" << maximumGeometryDifference
                << ')' << std::endl;
      result = 1;
    }

    geometryRoot->addChild(geometryOccurrence);
    geometryManager.render(TRUE, TRUE);
    coordinates->point.set1Value(0, SbVec3f(-0.5f, -0.8f, 0.0f));
    geometryManager.render(TRUE, TRUE);
    const SoRenderManager::RenderPhaseStatistics sharedGeometryPhases =
      geometryManager.getRenderPhaseStatistics();
    const std::vector<uint8_t> sharedGeometryPixels = context.readPixels();
    geometryManager.invalidateDrawList();
    geometryManager.render(TRUE, TRUE);
    const std::vector<uint8_t> rebuiltSharedGeometryPixels =
      context.readPixels();
    if (sharedGeometryPhases.drawListRebuilds != 0 ||
        sharedGeometryPhases.incrementalCommandUpdates != 2 ||
        sharedGeometryPixels != rebuiltSharedGeometryPixels) {
      std::cerr << "FAIL: shared geometry resource was not regenerated "
                   "transactionally (rebuilds="
                << sharedGeometryPhases.drawListRebuilds << ", updates="
                << sharedGeometryPhases.incrementalCommandUpdates
                << ", parity="
                << (sharedGeometryPixels == rebuiltSharedGeometryPixels)
                << ')'
                << std::endl;
      result = 1;
    }
    geometryRoot->unref();

    SoSeparator * visibilityRoot = new SoSeparator;
    SoSwitch * visibilitySwitch = new SoSwitch;
    visibilitySwitch->whichChild = SO_SWITCH_ALL;
    SoSeparator * visibilityBranch = new SoSeparator;
    SoTranslation * visibilityOffset = new SoTranslation;
    visibilityOffset->translation.setValue(0.0f, 0.0f, -3.0f);
    visibilityBranch->addChild(visibilityOffset);
    visibilityBranch->addChild(new SoCube);
    SoSeparator * authoredInvisible = new SoSeparator;
    SoDrawStyle * invisibleStyle = new SoDrawStyle;
    invisibleStyle->style = SoDrawStyle::INVISIBLE;
    authoredInvisible->addChild(invisibleStyle);
    authoredInvisible->addChild(new SoCube);
    visibilityBranch->addChild(authoredInvisible);
    visibilitySwitch->addChild(visibilityBranch);
    visibilityRoot->addChild(visibilitySwitch);
    visibilityRoot->ref();
    {
      SoRenderManager visibilityManager;
      visibilityManager.setViewportRegion(testViewport);
      visibilityManager.setSceneGraph(visibilityRoot);
      visibilityManager.setCamera(camera);
      visibilityManager.setRenderPipeline(
        SoRenderManager::RenderPipeline::DRAW_LIST);
      visibilityManager.render(TRUE, TRUE);
      const std::vector<uint8_t> visiblePixels = context.readPixels();

      visibilitySwitch->whichChild = SO_SWITCH_NONE;
      visibilityManager.render(TRUE, TRUE);
      const SoRenderManager::RenderPhaseStatistics hiddenPhases =
        visibilityManager.getRenderPhaseStatistics();
      const int hiddenPixels = countNonBlack(context);

      visibilitySwitch->whichChild = SO_SWITCH_ALL;
      visibilityManager.render(TRUE, TRUE);
      const SoRenderManager::RenderPhaseStatistics shownPhases =
        visibilityManager.getRenderPhaseStatistics();
      const std::vector<uint8_t> shownPixels = context.readPixels();
      if (hiddenPhases.drawListRebuilds != 0 ||
          hiddenPhases.incrementalCommandUpdates != 2 ||
          shownPhases.drawListRebuilds != 0 ||
          shownPhases.incrementalCommandUpdates != 2 ||
          hiddenPixels != 0 || shownPixels != visiblePixels) {
        std::cerr << "FAIL: stable one-child switch did not patch visibility"
                  << std::endl;
        result = 1;
      }
      visibilityManager.releaseRenderBackendResources();
      visibilityManager.setCamera(NULL);
      visibilityManager.setSceneGraph(NULL);
    }
    visibilityRoot->unref();

    SoSeparator * structuralSwitchRoot = new SoSeparator;
    SoSwitch * structuralSwitch = new SoSwitch;
    structuralSwitch->whichChild = 0;
    structuralSwitch->addChild(new SoCube);
    structuralSwitch->addChild(new SoCube);
    structuralSwitchRoot->addChild(structuralSwitch);
    structuralSwitchRoot->ref();
    {
      SoRenderManager structuralManager;
      structuralManager.setViewportRegion(testViewport);
      structuralManager.setSceneGraph(structuralSwitchRoot);
      structuralManager.setCamera(camera);
      structuralManager.setRenderPipeline(
        SoRenderManager::RenderPipeline::DRAW_LIST);
      structuralManager.render(TRUE, TRUE);
      structuralSwitch->whichChild = 1;
      structuralManager.render(TRUE, TRUE);
      const SoRenderManager::RenderPhaseStatistics structuralPhases =
        structuralManager.getRenderPhaseStatistics();
      if (structuralPhases.drawListRebuilds != 1 ||
          structuralPhases.incrementalCommandUpdates != 0) {
        std::cerr << "FAIL: structural switch change did not rebuild"
                  << std::endl;
        result = 1;
      }
      structuralManager.releaseRenderBackendResources();
      structuralManager.setCamera(NULL);
      structuralManager.setSceneGraph(NULL);
    }
    structuralSwitchRoot->unref();

    cubeRoot->touch();
    manager.render(TRUE, TRUE);
    const SoRenderManager::RenderPhaseStatistics changedRenderPhases =
      manager.getRenderPhaseStatistics();
    if (changedRenderPhases.drawListRebuilds != 1 ||
        changedRenderPhases.drawListConstructionNanoseconds == 0) {
      std::cerr << "FAIL: changed retained frame was not rebuilt" << std::endl;
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
    const SoRenderManager::RenderPhaseStatistics firstPickPhases =
      manager.getRenderPhaseStatistics();
    if (firstPickPhases.pickBufferRefreshes != 1 ||
        firstPickPhases.pickBufferUpdateNanoseconds == 0 ||
        firstPickPhases.pickQueryNanoseconds == 0 ||
        firstPickPhases.pickResultResolutionNanoseconds == 0 ||
        firstPickPhases.backendPickTargetPreparationNanoseconds == 0 ||
        firstPickPhases.backendPickTargetRenderingNanoseconds == 0 ||
        firstPickPhases.backendPickReadbackNanoseconds == 0 ||
        firstPickPhases.backendPickDepthRenderingNanoseconds != 0 ||
        firstPickPhases.backendPickDepthPeelingNanoseconds != 0 ||
        firstPickPhases.backendPickTargetRestoreNanoseconds != 0) {
      std::cerr << "FAIL: retained pick phases were not measured" << std::endl;
      result = 1;
    }

    cubeTranslation->translation.setValue(0.21f, 0.0f, -3.0f);
    manager.render(TRUE, TRUE);
    closest = NULL;
    if (!manager.pickClosest(16, 16, 1, closest) || !closest ||
        manager.getRenderPhaseStatistics().pickBufferRefreshes != 1) {
      std::cerr << "FAIL: incremental update did not refresh retained picking"
                << std::endl;
      result = 1;
    }
    delete closest;

    SoPickedPointList stack;
    if (!manager.pickDepthStack(16, 16, 0, 8, stack) ||
        stack.getLength() == 0 ||
        stack[0]->getPath()->getTail()->getTypeId() !=
          SoCube::getClassTypeId()) {
      std::cerr << "FAIL: manager depth-stack query did not return scene hits"
                << std::endl;
      result = 1;
    }
    const SoRenderManager::RenderPhaseStatistics reusedPickPhases =
      manager.getRenderPhaseStatistics();
    if (reusedPickPhases.pickBufferRefreshes != 0 ||
        reusedPickPhases.pickBufferUpdateNanoseconds != 0 ||
        reusedPickPhases.pickQueryNanoseconds == 0 ||
        reusedPickPhases.backendPickDepthRenderingNanoseconds == 0 ||
        reusedPickPhases.pickDrawCalls == 0 ||
        reusedPickPhases.backendPickTargetRestoreNanoseconds == 0) {
      std::cerr << "FAIL: reused pick buffer phases were misreported" << std::endl;
      result = 1;
    }

    manager.setRenderPhaseTimingEnabled(FALSE);
    const SoRenderManager::RenderPhaseStatistics disabledPhases =
      manager.getRenderPhaseStatistics();
    if (disabledPhases.drawListConstructionNanoseconds != 0 ||
        disabledPhases.pickQueryNanoseconds != 0) {
      std::cerr << "FAIL: disabling render phase timing did not reset statistics"
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
