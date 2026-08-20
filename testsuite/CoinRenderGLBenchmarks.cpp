#include "support/GLTestContext.h"
#include "rendering/SoGLRenderBackend.h"
#include "rendering/SoRenderPlan.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/system/gl.h>
#include <Inventor/actions/SoRayPickAction.h>
#if COIN_HAVE_LEGACY_GL_RENDERER
#include <Inventor/actions/SoGLRenderAction.h>
#endif
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTranslation.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

enum class WorkloadKind {
  ManyDraws,
  MaterialChurn,
  Transparency,
  DensePicking,
  FeatureRich,
  SharedAssemblyExpanded,
  SharedAssemblySources,
  SharedAssemblyRecipe
};

struct Options {
  bool smoke = false;
  int samples = 0;
  int rebuildOnly = 0;
  int incrementalOnly = 0;
  int assemblyOnly = 0;
  std::string output;
};

struct SceneMutationHandles {
  std::vector<SoTranslation *> transforms;
  std::vector<SoMaterial *> materials;
  std::vector<SoCoordinate3 *> coordinates;
  std::vector<SoCoordinate3 *> definitionCoordinates;
};

struct Measurement {
  std::string workload;
  std::string renderer;
  std::string profile;
  int semanticDraws = 0;
  int samples = 0;
  double cpuMedianMs = 0.0;
  double cpuP95Ms = 0.0;
  double gpuMedianMs = 0.0;
  double gpuP95Ms = 0.0;
  double completionMedianMs = 0.0;
  double completionP95Ms = 0.0;
  double drawListConstructionMs = 0.0;
  double planConstructionMs = 0.0;
  double commandPreparationMs = 0.0;
  double stateSetupMs = 0.0;
  double programBindingMs = 0.0;
  double drawSubmissionMs = 0.0;
  double coldPickMs = 0.0;
  double refreshPickMs = 0.0;
  double asyncPickSubmitMs = 0.0;
  double asyncPickReadyMs = 0.0;
  double asyncPickPollMaxMs = 0.0;
  double selectionMedianMs = 0.0;
  double selectionP95Ms = 0.0;
  double mutationMedianMs = 0.0;
  double mutationP95Ms = 0.0;
  double pickUpdateCpuMedianMs = 0.0;
  double pickUpdateCompletionMedianMs = 0.0;
  double pickIdOnlyMedianMs = 0.0;
  double asyncIdSubmitMedianMs = 0.0;
  double asyncIdReadyMedianMs = 0.0;
  double asyncIdPollMaxMs = 0.0;
  SoRenderStatistics renderStatistics;
  double pickMedianMs = 0.0;
  double pickP95Ms = 0.0;
  uint64_t pixelChecksum = 0;
};

double elapsedMs(const Clock::time_point & start)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double percentile(std::vector<double> values, double fraction)
{
  std::sort(values.begin(), values.end());
  size_t index = static_cast<size_t>(
    std::ceil(static_cast<double>(values.size()) * fraction)) - 1;
  return values[index];
}

const char * workloadName(WorkloadKind kind)
{
  switch (kind) {
  case WorkloadKind::ManyDraws: return "many_small_draws";
  case WorkloadKind::MaterialChurn: return "many_material_changes";
  case WorkloadKind::Transparency: return "transparent_sorting";
  case WorkloadKind::DensePicking: return "single_pick_dense_scene";
  case WorkloadKind::FeatureRich: return "feature_rich_scene_end_to_end";
  case WorkloadKind::SharedAssemblyExpanded:
    return "shared_assembly_expanded";
  case WorkloadKind::SharedAssemblySources:
    return "shared_assembly_sources";
  case WorkloadKind::SharedAssemblyRecipe:
    return "shared_assembly_recipe";
  }
  return "unknown";
}

bool isAssemblyWorkload(WorkloadKind kind)
{
  return kind == WorkloadKind::SharedAssemblyExpanded ||
    kind == WorkloadKind::SharedAssemblySources ||
    kind == WorkloadKind::SharedAssemblyRecipe;
}

int assemblyDefinitionCount(int occurrenceCount)
{
  return std::min(20, std::max(1,
    static_cast<int>(std::sqrt(static_cast<double>(occurrenceCount)))));
}

struct AssemblyPart {
  SoCoordinate3 * coordinates = nullptr;
  SoNormal * normals = nullptr;
  SoIndexedFaceSet * faces = nullptr;
  SoIndexedLineSet * edges = nullptr;
};

SoCoordinate3 * addAssemblyGeometry(SoSeparator * parent, WorkloadKind kind,
                                    const AssemblyPart & part,
                                    SoMaterial * faceMaterial,
                                    SoMaterial * edgeMaterial)
{
  SoIndexedFaceSet * faces = part.faces;
  SoIndexedLineSet * edges = part.edges;
  SoCoordinate3 * coordinates = part.coordinates;
  if (kind == WorkloadKind::SharedAssemblyExpanded) {
    coordinates = new SoCoordinate3;
    coordinates->point = part.coordinates->point;
    SoNormal * normals = new SoNormal;
    normals->vector = part.normals->vector;
    faces = new SoIndexedFaceSet;
    faces->coordIndex = part.faces->coordIndex;
    edges = new SoIndexedLineSet;
    edges->coordIndex = part.edges->coordIndex;
    parent->addChild(coordinates);
    parent->addChild(normals);
  }
  else if (kind == WorkloadKind::SharedAssemblySources) {
    faces = new SoIndexedFaceSet;
    faces->coordIndex = part.faces->coordIndex;
    edges = new SoIndexedLineSet;
    edges->coordIndex = part.edges->coordIndex;
  }

  SoSeparator * faceBranch = new SoSeparator;
  faceBranch->renderCaching = SoSeparator::OFF;
  faceBranch->addChild(faceMaterial);
  faceBranch->addChild(faces);
  parent->addChild(faceBranch);
  SoSeparator * edgeBranch = new SoSeparator;
  edgeBranch->renderCaching = SoSeparator::OFF;
  edgeBranch->addChild(edgeMaterial);
  edgeBranch->addChild(edges);
  parent->addChild(edgeBranch);
  return coordinates;
}

void populateAssemblyScene(SoSeparator * root, WorkloadKind kind,
                           int occurrenceCount,
                           SceneMutationHandles * mutations = nullptr)
{
  // A small deterministic set of reusable definitions is enough to expose
  // the ownership difference. Geometry size varies by definition so the
  // workload also contains a realistic mixture of small and medium parts.
  const int definitionCount = assemblyDefinitionCount(occurrenceCount);
  std::vector<AssemblyPart> parts(static_cast<size_t>(definitionCount));
  for (int definition = 0; definition < definitionCount; ++definition) {
    AssemblyPart & part = parts[static_cast<size_t>(definition)];
    part.coordinates = new SoCoordinate3;
    part.normals = new SoNormal;
    part.faces = new SoIndexedFaceSet;
    part.edges = new SoIndexedLineSet;
    const int triangleCount = 8 + (definition % 5) * 8;
    std::vector<SbVec3f> positions;
    std::vector<SbVec3f> normals;
    std::vector<int32_t> faceIndices;
    std::vector<int32_t> edgeIndices;
    positions.reserve(static_cast<size_t>(triangleCount + 1));
    normals.reserve(static_cast<size_t>(triangleCount + 1));
    faceIndices.reserve(static_cast<size_t>(triangleCount * 4));
    edgeIndices.reserve(static_cast<size_t>(triangleCount * 3));
    positions.push_back(SbVec3f(0.0f, 0.0f, 0.04f));
    normals.push_back(SbVec3f(0.0f, 0.0f, 1.0f));
    for (int vertex = 0; vertex < triangleCount; ++vertex) {
      const float angle = static_cast<float>(vertex) *
        6.28318530718f / static_cast<float>(triangleCount);
      const float radius = 0.25f + 0.015f * static_cast<float>(definition);
      positions.push_back(SbVec3f(std::cos(angle) * radius,
                                  std::sin(angle) * radius,
                                  0.01f * static_cast<float>(vertex % 3)));
      normals.push_back(SbVec3f(0.0f, 0.0f, 1.0f));
      const int current = vertex + 1;
      const int next = ((vertex + 1) % triangleCount) + 1;
      faceIndices.push_back(0);
      faceIndices.push_back(current);
      faceIndices.push_back(next);
      faceIndices.push_back(-1);
      edgeIndices.push_back(current);
      edgeIndices.push_back(next);
      edgeIndices.push_back(-1);
    }
    part.coordinates->point.setValues(0, static_cast<int>(positions.size()),
                                      positions.data());
    part.normals->vector.setValues(0, static_cast<int>(normals.size()),
                                   normals.data());
    part.faces->coordIndex.setValues(0, static_cast<int>(faceIndices.size()),
                                     faceIndices.data());
    part.edges->coordIndex.setValues(0, static_cast<int>(edgeIndices.size()),
                                     edgeIndices.data());
    if (mutations) mutations->definitionCoordinates.push_back(part.coordinates);
  }

  SoNormalBinding * normalBinding = new SoNormalBinding;
  normalBinding->value = SoNormalBinding::PER_VERTEX;
  root->addChild(normalBinding);
  std::vector<SoSeparator *> definitionBranches(
    static_cast<size_t>(definitionCount));
  for (int definition = 0; definition < definitionCount; ++definition) {
    SoSeparator * branch = new SoSeparator;
    branch->renderCaching = SoSeparator::OFF;
    if (kind != WorkloadKind::SharedAssemblyExpanded) {
      const AssemblyPart & part = parts[static_cast<size_t>(definition)];
      branch->addChild(part.coordinates);
      branch->addChild(part.normals);
    }
    definitionBranches[static_cast<size_t>(definition)] = branch;
    root->addChild(branch);
  }
  const int columns = static_cast<int>(std::ceil(std::sqrt(
    static_cast<double>(occurrenceCount))));
  const int occurrencesPerDefinition =
    (occurrenceCount + definitionCount - 1) / definitionCount;
  for (int occurrence = 0; occurrence < occurrenceCount; ++occurrence) {
    SoSeparator * instance = new SoSeparator;
    instance->renderCaching = SoSeparator::OFF;
    SoTranslation * placement = new SoTranslation;
    placement->translation.setValue(
      (static_cast<float>(occurrence % columns) - columns * 0.5f) * 0.65f,
      (static_cast<float>(occurrence / columns) - columns * 0.5f) * 0.65f,
      -0.002f * static_cast<float>(occurrence % 7));
    instance->addChild(placement);
    if (mutations) mutations->transforms.push_back(placement);
    SoMaterial * material = new SoMaterial;
    const int shade = occurrence % 4;
    material->diffuseColor.setValue(0.25f + 0.18f * shade,
                                    0.75f - 0.12f * shade,
                                    0.35f + 0.10f * shade);
    if (mutations) mutations->materials.push_back(material);
    SoMaterial * edgeMaterial = new SoMaterial;
    edgeMaterial->diffuseColor.setValue(0.08f, 0.08f, 0.10f);
    const int definition = std::min(definitionCount - 1,
      occurrence / occurrencesPerDefinition);
    SoCoordinate3 * occurrenceCoordinates = addAssemblyGeometry(instance, kind,
      parts[static_cast<size_t>(definition)], material, edgeMaterial);
    if (mutations) mutations->coordinates.push_back(occurrenceCoordinates);
    definitionBranches[static_cast<size_t>(definition)]->addChild(instance);
  }
}

SoSeparator * makeScene(WorkloadKind kind, int drawCount,
                        SoOrthographicCamera *& camera,
                        SceneMutationHandles * mutations = nullptr)
{
  SoSeparator * root = new SoSeparator;
  root->ref();
  root->renderCaching = SoSeparator::OFF;
  camera = new SoOrthographicCamera;
  camera->ref();
  camera->position.setValue(0.0f, 0.0f, 10.0f);
  camera->height = 24.0f;
  camera->nearDistance = 0.1f;
  camera->farDistance = 100.0f;
  camera->focalDistance = 10.0f;
  SoLightModel * lightModel = new SoLightModel;
  lightModel->model = kind == WorkloadKind::FeatureRich
    ? SoLightModel::PHONG : SoLightModel::BASE_COLOR;
  root->addChild(lightModel);
  if (kind == WorkloadKind::FeatureRich) {
    SoDirectionalLight * light = new SoDirectionalLight;
    light->direction.setValue(0.0f, 0.0f, -1.0f);
    root->addChild(light);
  }
  SoMaterial * defaultMaterial = new SoMaterial;
  defaultMaterial->diffuseColor.setValue(0.3f, 0.7f, 1.0f);
  root->addChild(defaultMaterial);

  if (isAssemblyWorkload(kind)) {
    camera->height = std::max(8.0f, static_cast<float>(
      std::ceil(std::sqrt(static_cast<double>(drawCount))) * 0.7));
    populateAssemblyScene(root, kind, drawCount, mutations);
    return root;
  }

  const SbVec3f triangle[] = {
    SbVec3f(-0.42f, -0.42f, 0.0f),
    SbVec3f(0.42f, -0.42f, 0.0f),
    SbVec3f(0.0f, 0.42f, 0.0f)
  };
  const int columns = static_cast<int>(std::ceil(std::sqrt(
    static_cast<double>(drawCount))));
  const int rows = (drawCount + columns - 1) / columns;
  SoCoordinate3 * sharedCoordinates = nullptr;
  SoNormal * sharedNormals = nullptr;
  SoNormalBinding * sharedNormalBinding = nullptr;
  SoTextureCoordinate2 * sharedTexcoords = nullptr;
  SoTexture2 * sharedTexture = nullptr;
  SoMaterial * litMaterial = nullptr;
  SoMaterial * vertexMaterial = nullptr;
  SoMaterialBinding * vertexBinding = nullptr;
  SoMaterial * transparentMaterial = nullptr;
  SoFaceSet * sharedFace = nullptr;
  if (kind == WorkloadKind::FeatureRich) {
    sharedCoordinates = new SoCoordinate3;
    sharedCoordinates->point.setValues(0, 3, triangle);
    const SbVec3f normals[] = {
      SbVec3f(0.0f, 0.0f, 1.0f), SbVec3f(0.0f, 0.0f, 1.0f),
      SbVec3f(0.0f, 0.0f, 1.0f)
    };
    sharedNormals = new SoNormal;
    sharedNormals->vector.setValues(0, 3, normals);
    sharedNormalBinding = new SoNormalBinding;
    sharedNormalBinding->value = SoNormalBinding::PER_VERTEX;
    const SbVec2f textureCoordinates[] = {
      SbVec2f(0.0f, 0.0f), SbVec2f(1.0f, 0.0f), SbVec2f(0.5f, 1.0f)
    };
    sharedTexcoords = new SoTextureCoordinate2;
    sharedTexcoords->point.setValues(0, 3, textureCoordinates);
    const unsigned char texels[] = {
      220, 80, 40, 255, 40, 180, 220, 255,
      40, 180, 220, 255, 220, 80, 40, 255
    };
    sharedTexture = new SoTexture2;
    sharedTexture->image.setValue(SbVec2s(2, 2), 4, texels);
    litMaterial = new SoMaterial;
    litMaterial->diffuseColor.setValue(0.65f, 0.72f, 0.85f);
    const SbColor vertexColors[] = {
      SbColor(1.0f, 0.2f, 0.2f), SbColor(0.2f, 1.0f, 0.2f),
      SbColor(0.2f, 0.2f, 1.0f)
    };
    vertexMaterial = new SoMaterial;
    vertexMaterial->diffuseColor.setValues(0, 3, vertexColors);
    vertexBinding = new SoMaterialBinding;
    vertexBinding->value = SoMaterialBinding::PER_VERTEX;
    transparentMaterial = new SoMaterial;
    transparentMaterial->diffuseColor.setValue(0.65f, 0.72f, 0.85f);
    transparentMaterial->transparency = 0.45f;
    sharedFace = new SoFaceSet;
    sharedFace->numVertices.set1Value(0, 3);
  }
  for (int i = 0; i < drawCount; ++i) {
    SoSeparator * draw = new SoSeparator;
    draw->renderCaching = SoSeparator::OFF;
    SoTranslation * translation = new SoTranslation;
    const float x = kind == WorkloadKind::DensePicking ? 0.0f :
      (static_cast<float>(i % columns) -
       static_cast<float>(columns - 1) * 0.5f) * 1.05f;
    const float y = kind == WorkloadKind::DensePicking ? 0.0f :
      (static_cast<float>(i / columns) -
       static_cast<float>(rows - 1) * 0.5f) * 1.05f;
    const float z = kind == WorkloadKind::Transparency
      ? -static_cast<float>(i % 32) * 0.01f
      : (kind == WorkloadKind::DensePicking
         ? -static_cast<float>(i) * 0.001f : 0.0f);
    translation->translation.setValue(x, y, z);
    draw->addChild(translation);
    if (mutations) mutations->transforms.push_back(translation);

    if (kind == WorkloadKind::FeatureRich) {
      const int group = i < drawCount * 2 / 5 ? 0
        : (i < drawCount * 3 / 5 ? 1
          : (i < drawCount * 4 / 5 ? 2 : 3));
      if (group == 2) {
        draw->addChild(vertexMaterial);
        draw->addChild(vertexBinding);
      }
      else {
        draw->addChild(group == 3 ? transparentMaterial : litMaterial);
      }
      if (group == 1) {
        draw->addChild(sharedTexture);
        draw->addChild(sharedTexcoords);
      }
      draw->addChild(sharedCoordinates);
      draw->addChild(sharedNormals);
      draw->addChild(sharedNormalBinding);
      draw->addChild(sharedFace);
    }
    else if (kind != WorkloadKind::ManyDraws) {
      SoMaterial * material = new SoMaterial;
      const float value = static_cast<float>((i * 17) % 101) / 100.0f;
      material->diffuseColor.setValue(0.2f + value * 0.8f,
                                      0.9f - value * 0.7f,
                                      0.3f + value * 0.5f);
      if (kind == WorkloadKind::Transparency) material->transparency = 0.35f;
      draw->addChild(material);
      if (mutations) mutations->materials.push_back(material);
    }
    if (kind != WorkloadKind::FeatureRich) {
      SoCoordinate3 * coordinates = new SoCoordinate3;
      coordinates->point.setValues(0, 3, triangle);
      if (mutations) mutations->coordinates.push_back(coordinates);
      SoFaceSet * face = new SoFaceSet;
      face->numVertices.set1Value(0, 3);
      draw->addChild(coordinates);
      draw->addChild(face);
    }
    root->addChild(draw);
  }
  return root;
}

uint64_t checksumPixels(const std::vector<uint8_t> & pixels)
{
  uint64_t hash = 1469598103934665603ULL;
  bool nonBlack = false;
  for (size_t i = 0; i < pixels.size(); ++i) {
    hash ^= pixels[i];
    hash *= 1099511628211ULL;
    if ((i % 4) != 3 && pixels[i] > 4) nonBlack = true;
  }
  return nonBlack ? hash : 0;
}

bool checkTimerQueries()
{
#ifdef GL_TIME_ELAPSED
  GLuint query = 0;
  glGenQueries(1, &query);
  if (query == 0 || glGetError() != GL_NO_ERROR) return false;
  glDeleteQueries(1, &query);
  return true;
#else
  return false;
#endif
}

bool runVariant(GLTestProfile profile,
                SoRenderManager::RenderPipeline pipeline,
                const std::string & renderer, WorkloadKind workload,
                int drawCount, int samples, Measurement & result,
                std::string & unavailable, bool forceDrawListRebuild = false)
{
  GLTestContextConfig config;
  config.profile = profile;
  config.major = 3;
  config.minor = 3;
  config.width = 256;
  config.height = 256;
  GLTestContext context;
  if (!context.initialize(config)) {
    unavailable = "requested OpenGL context is unavailable";
    return false;
  }
  if (!checkTimerQueries()) {
    unavailable = "OpenGL timer queries are unavailable";
    return false;
  }

  SoOrthographicCamera * camera = NULL;
  SoSeparator * scene = makeScene(workload, drawCount, camera);
  SbViewportRegion viewport(SbVec2s(256, 256));
  viewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(256, 256));
#if COIN_HAVE_LEGACY_GL_RENDERER
  SoGLRenderAction legacyAction(viewport);
  legacyAction.setCacheContext(context.contextId());
  legacyAction.setTransparencyType(SoGLRenderAction::SORTED_OBJECT_BLEND);
#endif
  SoRenderManager manager;
  manager.setViewportRegion(viewport);
  manager.setSceneGraph(scene);
  manager.setCamera(camera);
  manager.setLightingMode(workload == WorkloadKind::FeatureRich
                            ? SoRenderManager::LIT
                            : SoRenderManager::UNLIT);
  manager.setRenderPipeline(pipeline);
  manager.setRenderPhaseTimingEnabled(
    pipeline == SoRenderManager::RenderPipeline::DRAW_LIST);
#if COIN_HAVE_LEGACY_GL_RENDERER
  if (pipeline == SoRenderManager::RenderPipeline::LEGACY_GL) {
    manager.setGLRenderAction(&legacyAction);
  }
#endif

  for (int warmup = 0; warmup < 5; ++warmup) {
    context.bindFramebuffer();
    manager.render(TRUE, TRUE);
  }
  glFinish();
  if (manager.getLastRenderResult().usedPipeline != pipeline ||
      !manager.getLastRenderResult().rendered) {
    unavailable = "renderer manager fell back from the requested pipeline";
    camera->unref();
    scene->unref();
    return false;
  }

  std::vector<double> cpu;
  std::vector<double> gpu;
  std::vector<double> completion;
  std::vector<double> drawListConstruction;
  std::vector<double> planConstruction;
  std::vector<double> commandPreparation;
  std::vector<double> stateSetup;
  std::vector<double> programBinding;
  std::vector<double> drawSubmission;
  GLuint query = 0;
  glGenQueries(1, &query);
  for (int sample = 0; sample < samples; ++sample) {
    context.bindFramebuffer();
    const Clock::time_point totalStart = Clock::now();
    glBeginQuery(GL_TIME_ELAPSED, query);
    const Clock::time_point cpuStart = Clock::now();
    if (forceDrawListRebuild &&
        pipeline == SoRenderManager::RenderPipeline::DRAW_LIST) {
      manager.invalidateDrawList();
    }
    manager.render(TRUE, TRUE);
    cpu.push_back(elapsedMs(cpuStart));
    const SoRenderStatistics sampleStatistics = manager.getRenderStatistics();
    drawListConstruction.push_back(
      sampleStatistics.drawListConstructionNanoseconds / 1000000.0);
    planConstruction.push_back(
      sampleStatistics.planConstructionNanoseconds / 1000000.0);
    commandPreparation.push_back(
      sampleStatistics.commandPreparationNanoseconds / 1000000.0);
    stateSetup.push_back(sampleStatistics.stateSetupNanoseconds / 1000000.0);
    programBinding.push_back(
      sampleStatistics.programBindingNanoseconds / 1000000.0);
    drawSubmission.push_back(
      sampleStatistics.drawSubmissionNanoseconds / 1000000.0);
    glEndQuery(GL_TIME_ELAPSED);
    GLuint64 nanoseconds = 0;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
    completion.push_back(elapsedMs(totalStart));
    gpu.push_back(static_cast<double>(nanoseconds) / 1000000.0);
  }
  glDeleteQueries(1, &query);
  const SoRenderStatistics renderStatistics = manager.getRenderStatistics();
  if (pipeline == SoRenderManager::RenderPipeline::DRAW_LIST) {
    if (isAssemblyWorkload(workload)) {
      const uint64_t expectedCommands = static_cast<uint64_t>(drawCount) * 2;
      const uint64_t sharedResources =
        static_cast<uint64_t>(assemblyDefinitionCount(drawCount)) * 2;
      const uint64_t expectedResources =
        workload == WorkloadKind::SharedAssemblyRecipe
        ? sharedResources : expectedCommands;
      const uint64_t maximumResources =
        workload == WorkloadKind::SharedAssemblyRecipe
        ? sharedResources + static_cast<uint64_t>(
            assemblyDefinitionCount(drawCount))
        : expectedCommands;
      if (renderStatistics.retainedCommands !=
            expectedCommands ||
          renderStatistics.retainedGeometryResources < expectedResources ||
          renderStatistics.retainedGeometryResources > maximumResources) {
        std::cerr << "FAIL: " << workloadName(workload)
                  << " retained unexpected ownership counts"
                  << " (commands=" << renderStatistics.retainedCommands
                  << ", resources="
                  << renderStatistics.retainedGeometryResources
                  << ", expected-resources=" << expectedResources << ".."
                  << maximumResources << ")\n";
        camera->unref();
        scene->unref();
        return false;
      }
    }
    const bool expectedInstanceCoverage = drawCount >= 20
      ? renderStatistics.instancedCommands == static_cast<uint64_t>(drawCount)
      : renderStatistics.instancedCommands != 0;
    const bool expectedAssembly = isAssemblyWorkload(workload);
    const uint64_t minimumGroupedCommands = static_cast<uint64_t>(
      std::max(0, drawCount - assemblyDefinitionCount(drawCount))) * 2;
    const bool expectedBatching = expectedAssembly
      ? (workload == WorkloadKind::SharedAssemblyExpanded ||
         (renderStatistics.instancedCommands >= minimumGroupedCommands &&
          renderStatistics.drawCalls < static_cast<uint64_t>(drawCount)))
      : workload == WorkloadKind::FeatureRich
      ? expectedInstanceCoverage &&
        renderStatistics.drawCalls < static_cast<uint64_t>(drawCount) &&
        renderStatistics.instanceBreakGeometryResource != 0 &&
        renderStatistics.instanceRejectedMaterial == 0
      : renderStatistics.instancedCommands ==
          static_cast<uint64_t>(drawCount) &&
        renderStatistics.drawCalls == 1;
    if (!expectedBatching) {
      std::cerr << "FAIL: " << renderer << ' ' << workloadName(workload)
                << " did not retain expected batching"
                << " (draws=" << renderStatistics.drawCalls
                << ", instances=" << renderStatistics.instancedCommands
                << ", material-rejected="
                << renderStatistics.instanceRejectedMaterial
                << ", geometry-breaks="
                << renderStatistics.instanceBreakGeometryResource
                << ", material-breaks="
                << renderStatistics.instanceBreakMaterial
                << ", plan-breaks="
                << renderStatistics.instanceBreakPlanBoundary << ")\n";
      camera->unref();
      scene->unref();
      return false;
    }
  }

  std::vector<double> pick;
  double coldPick = 0.0;
  double refreshPick = 0.0;
  double asyncPickSubmit = 0.0;
  double asyncPickReady = 0.0;
  double asyncPickPollMax = 0.0;
  if (workload == WorkloadKind::DensePicking) {
    SoSeparator * legacyPickRoot = NULL;
#if COIN_HAVE_LEGACY_GL_RENDERER
    if (pipeline == SoRenderManager::RenderPipeline::LEGACY_GL) {
      legacyPickRoot = new SoSeparator;
      legacyPickRoot->ref();
      legacyPickRoot->addChild(camera);
      legacyPickRoot->addChild(scene);
    }
#endif
    auto performPick = [&]() {
      SoPickedPoint * picked = NULL;
      SbBool hit = FALSE;
#if COIN_HAVE_LEGACY_GL_RENDERER
      if (pipeline == SoRenderManager::RenderPipeline::LEGACY_GL) {
        SoRayPickAction action(viewport);
        action.setPoint(SbVec2s(128, 128));
        action.setRadius(4.0f);
        action.apply(legacyPickRoot);
        if (action.getPickedPoint()) {
          picked = new SoPickedPoint(*action.getPickedPoint());
          hit = TRUE;
        }
      }
      else
#endif
      {
        hit = manager.pickClosest(128, 128, 4, picked);
      }
      if (!hit || !picked) {
        std::cerr << "FAIL: " << renderer
                  << " dense picking did not return a hit\n";
        std::exit(1);
      }
      delete picked;
    };
    const Clock::time_point coldPickStart = Clock::now();
    performPick();
    coldPick = elapsedMs(coldPickStart);
    for (int sample = 0; sample < samples; ++sample) {
      const Clock::time_point pickStart = Clock::now();
      performPick();
      pick.push_back(elapsedMs(pickStart));
    }
    scene->touch();
    context.bindFramebuffer();
    manager.render(TRUE, TRUE);
#if COIN_HAVE_LEGACY_GL_RENDERER
    if (pipeline != SoRenderManager::RenderPipeline::LEGACY_GL)
#endif
    {
      SoAsyncPickRequest request;
      const Clock::time_point asyncStart = Clock::now();
      if (!manager.requestPickClosestAsync(128, 128, 4, request)) {
        std::cerr << "FAIL: asynchronous benchmark pick was rejected\n";
        std::exit(1);
      }
      asyncPickSubmit = elapsedMs(asyncStart);
      SoAsyncPickStatus status = SoAsyncPickStatus::PENDING;
      SoPickedPoint * asyncResult = NULL;
      while (status == SoAsyncPickStatus::PENDING) {
        const Clock::time_point pollStart = Clock::now();
        status = manager.pollPickClosestAsync(request, asyncResult);
        asyncPickPollMax = std::max(asyncPickPollMax, elapsedMs(pollStart));
        if (elapsedMs(asyncStart) > 1000.0) break;
        if (status == SoAsyncPickStatus::PENDING) std::this_thread::yield();
      }
      asyncPickReady = elapsedMs(asyncStart);
      if (status != SoAsyncPickStatus::HIT || !asyncResult) {
        std::cerr << "FAIL: asynchronous benchmark pick did not resolve\n";
        std::exit(1);
      }
      delete asyncResult;
      scene->touch();
      context.bindFramebuffer();
      manager.render(TRUE, TRUE);
    }
    const Clock::time_point refreshPickStart = Clock::now();
    performPick();
    refreshPick = elapsedMs(refreshPickStart);
    if (legacyPickRoot) legacyPickRoot->unref();
  }

  const uint64_t pixelChecksum = checksumPixels(context.readPixels());
  if (pixelChecksum == 0) {
    std::cerr << "FAIL: " << renderer << ' ' << workloadName(workload)
              << " rendered an empty frame\n";
    camera->unref();
    scene->unref();
    std::exit(1);
  }
  manager.releaseRenderBackendResources();
  manager.setCamera(NULL);
  manager.setSceneGraph(NULL);
  camera->unref();
  scene->unref();

  result.workload = workloadName(workload);
  result.renderer = renderer;
  result.profile = profile == GLTestProfile::Core ? "core" : "compatibility";
  result.semanticDraws = drawCount;
  result.samples = samples;
  result.cpuMedianMs = percentile(cpu, 0.5);
  result.cpuP95Ms = percentile(cpu, 0.95);
  result.gpuMedianMs = percentile(gpu, 0.5);
  result.gpuP95Ms = percentile(gpu, 0.95);
  result.completionMedianMs = percentile(completion, 0.5);
  result.completionP95Ms = percentile(completion, 0.95);
  result.drawListConstructionMs = percentile(drawListConstruction, 0.5);
  result.planConstructionMs = percentile(planConstruction, 0.5);
  result.commandPreparationMs = percentile(commandPreparation, 0.5);
  result.stateSetupMs = percentile(stateSetup, 0.5);
  result.programBindingMs = percentile(programBinding, 0.5);
  result.drawSubmissionMs = percentile(drawSubmission, 0.5);
  if (!pick.empty()) {
    result.coldPickMs = coldPick;
    result.refreshPickMs = refreshPick;
    result.asyncPickSubmitMs = asyncPickSubmit;
    result.asyncPickReadyMs = asyncPickReady;
    result.asyncPickPollMaxMs = asyncPickPollMax;
    result.pickMedianMs = percentile(pick, 0.5);
    result.pickP95Ms = percentile(pick, 0.95);
  }
  result.pixelChecksum = pixelChecksum;
  result.renderStatistics = renderStatistics;
  return true;
}

bool runIndexedInstances(GLTestProfile profile, int instanceCount, int samples,
                         Measurement & result, std::string & unavailable,
                         bool lineInstances = false)
{
  GLTestContextConfig config;
  config.profile = profile;
  config.major = 3;
  config.minor = 3;
  config.width = 256;
  config.height = 256;
  GLTestContext context;
  if (!context.initialize(config)) {
    unavailable = "requested OpenGL context is unavailable";
    return false;
  }
  if (!checkTimerQueries()) {
    unavailable = "OpenGL timer queries are unavailable";
    return false;
  }

  const float positions[] = {
    -0.03f, -0.03f, 0.0f,  0.03f, -0.03f, 0.0f,
     0.03f,  0.03f, 0.0f, -0.03f,  0.03f, 0.0f
  };
  const uint32_t triangleIndices[] = { 0, 1, 2, 0, 2, 3 };
  const uint32_t lineIndices[] = { 0, 1, 1, 2, 2, 3, 3, 0 };
  const int columns = static_cast<int>(std::ceil(std::sqrt(
    static_cast<double>(instanceCount))));
  const int rows = (instanceCount + columns - 1) / columns;
  const float xSpacing = columns > 1 ? 1.8f / static_cast<float>(columns - 1) : 0.0f;
  const float ySpacing = rows > 1 ? 1.8f / static_cast<float>(rows - 1) : 0.0f;
  SoDrawList drawlist;
  for (int i = 0; i < instanceCount; ++i) {
    SoRenderCommand command;
    const float x = columns > 1 ? -0.9f + (i % columns) * xSpacing : 0.0f;
    const float y = rows > 1 ? -0.9f + (i / columns) * ySpacing : 0.0f;
    command.modelMatrix.setTranslate(SbVec3f(x, y, 0.0f));
    command.geometry.topology = lineInstances
      ? SO_TOPOLOGY_LINES : SO_TOPOLOGY_TRIANGLES;
    command.geometry.vertexCount = 4;
    command.geometry.indexCount = lineInstances ? 8 : 6;
    command.geometry.positions = positions;
    command.geometry.indices = lineInstances ? lineIndices : triangleIndices;
    command.geometry.vertexStride = sizeof(float) * 3;
    command.geometry.cacheKey = 0x494e5354414e4345ULL;
    command.geometry.revision = 1;
    command.objectId = static_cast<SoObjectId>(i + 1);
    const float shade = static_cast<float>((i * 17) % 101) / 100.0f;
    command.material.diffuse = SbVec4f(0.25f + shade * 0.7f,
                                       0.85f - shade * 0.5f,
                                       0.35f + shade * 0.4f, 1.0f);
    drawlist.addCommand(command);
  }

  SoRenderParams params;
  params.viewport = SbViewportRegion(256, 256);
  params.viewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(256, 256));
  params.viewMatrix.makeIdentity();
  params.projMatrix.makeIdentity();
  params.clearColor.setValue(0.0f, 0.0f, 0.0f, 1.0f);
  params.clearDepth = 1.0f;
  params.flags = SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH;
  SoRenderPlanner planner;
  SoRenderPlan plan;
  drawlist.buildPickLUT();
  planner.build(drawlist, params.viewMatrix, plan);
  SoGLRenderBackend backend;
  SoRenderBackendInitParams initParams;
  if (!backend.initialize(initParams)) {
    unavailable = "retained OpenGL backend initialization failed";
    return false;
  }
  backend.setPhaseTimingEnabled(TRUE);

  for (int warmup = 0; warmup < 5; ++warmup) {
    context.bindFramebuffer();
    if (!backend.render(drawlist, plan, params)) {
      backend.shutdown();
      unavailable = "indexed retained render failed";
      return false;
    }
  }
  glFinish();

  std::vector<double> cpu;
  std::vector<double> gpu;
  std::vector<double> completion;
  std::vector<double> commandPreparation;
  std::vector<double> stateSetup;
  std::vector<double> programBinding;
  std::vector<double> drawSubmission;
  GLuint query = 0;
  glGenQueries(1, &query);
  for (int sample = 0; sample < samples; ++sample) {
    context.bindFramebuffer();
    const Clock::time_point totalStart = Clock::now();
    glBeginQuery(GL_TIME_ELAPSED, query);
    const Clock::time_point cpuStart = Clock::now();
    backend.render(drawlist, plan, params);
    cpu.push_back(elapsedMs(cpuStart));
    const SoRenderStatistics statistics = backend.getRenderStatistics();
    commandPreparation.push_back(statistics.commandPreparationNanoseconds / 1000000.0);
    stateSetup.push_back(statistics.stateSetupNanoseconds / 1000000.0);
    programBinding.push_back(statistics.programBindingNanoseconds / 1000000.0);
    drawSubmission.push_back(statistics.drawSubmissionNanoseconds / 1000000.0);
    glEndQuery(GL_TIME_ELAPSED);
    GLuint64 nanoseconds = 0;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
    completion.push_back(elapsedMs(totalStart));
    gpu.push_back(static_cast<double>(nanoseconds) / 1000000.0);
  }
  glDeleteQueries(1, &query);

  SoRenderStatistics statistics = backend.getRenderStatistics();
  const uint64_t pixelChecksum = checksumPixels(context.readPixels());
  context.bindFramebuffer();
  if (!backend.updatePickBuffer(drawlist, plan, params)) {
    unavailable = "indexed instance picking render failed";
    backend.shutdown();
    return false;
  }
  const SoRenderStatistics pickStatistics = backend.getRenderStatistics();
  statistics.pickDrawCalls = pickStatistics.pickDrawCalls;
  statistics.pickInstancedBatches = pickStatistics.pickInstancedBatches;
  statistics.pickInstancedEntries = pickStatistics.pickInstancedEntries;
  const bool expectedBatch = instanceCount > 1;
  if (statistics.drawCalls != 1 ||
      (expectedBatch && (statistics.instancedCommands !=
                           static_cast<uint64_t>(instanceCount) ||
                         statistics.drawCallsAvoided !=
                           static_cast<uint64_t>(instanceCount - 1) ||
                         statistics.pickDrawCalls != 1 ||
                         statistics.pickInstancedEntries !=
                           static_cast<uint64_t>(instanceCount))) ||
      pixelChecksum == 0) {
    std::cerr << "FAIL: indexed instance workload did not render as one "
                 "correct batch\n";
    backend.shutdown();
    return false;
  }
  backend.shutdown();

  result.workload = std::string(lineInstances
      ? "indexed_line_instances_" : "indexed_instances_") +
    std::to_string(instanceCount);
  result.renderer = "DrawList";
  result.profile = profile == GLTestProfile::Core ? "core" : "compatibility";
  result.semanticDraws = instanceCount;
  result.samples = samples;
  result.cpuMedianMs = percentile(cpu, 0.5);
  result.cpuP95Ms = percentile(cpu, 0.95);
  result.gpuMedianMs = percentile(gpu, 0.5);
  result.gpuP95Ms = percentile(gpu, 0.95);
  result.completionMedianMs = percentile(completion, 0.5);
  result.completionP95Ms = percentile(completion, 0.95);
  result.commandPreparationMs = percentile(commandPreparation, 0.5);
  result.stateSetupMs = percentile(stateSetup, 0.5);
  result.programBindingMs = percentile(programBinding, 0.5);
  result.drawSubmissionMs = percentile(drawSubmission, 0.5);
  result.renderStatistics = statistics;
  result.pixelChecksum = pixelChecksum;
  return true;
}

bool runFeatureRichScene(GLTestProfile profile, int commandCount, int samples,
                         Measurement & result, std::string & unavailable)
{
  GLTestContextConfig config;
  config.profile = profile;
  config.major = 3;
  config.minor = 3;
  config.width = 256;
  config.height = 256;
  GLTestContext context;
  if (!context.initialize(config) || !checkTimerQueries()) {
    unavailable = "required OpenGL context or timer queries are unavailable";
    return false;
  }

  const float positions[] = {
    -0.025f, -0.025f, 0.0f, 0.025f, -0.025f, 0.0f,
     0.025f,  0.025f, 0.0f, -0.025f, 0.025f, 0.0f
  };
  const float normals[] = {
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f
  };
  const float texcoords[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f
  };
  const float colors[] = {
    1.0f, 0.2f, 0.2f, 1.0f, 0.2f, 1.0f, 0.2f, 1.0f,
    0.2f, 0.2f, 1.0f, 1.0f, 1.0f, 0.8f, 0.2f, 1.0f
  };
  const uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };
  const unsigned char texels[] = {
    220, 80, 40, 255, 40, 180, 220, 255,
    40, 180, 220, 255, 220, 80, 40, 255
  };
  const int litCount = commandCount * 2 / 5;
  const int texturedCount = commandCount / 5;
  const int coloredCount = commandCount / 5;
  const int columns = static_cast<int>(std::ceil(std::sqrt(
    static_cast<double>(commandCount))));
  const int rows = (commandCount + columns - 1) / columns;
  const float dx = 1.8f / static_cast<float>(std::max(1, columns - 1));
  const float dy = 1.8f / static_cast<float>(std::max(1, rows - 1));

  SoDrawList drawlist;
  drawlist.reserve(commandCount);
  SoLightingData lighting;
  SoLightData directional;
  directional.direction.setValue(0.0f, 0.0f, 1.0f);
  lighting.lights.push_back(directional);
  const SoLightingHandle lightingHandle = drawlist.addLightingSetup(lighting);
  for (int i = 0; i < commandCount; ++i) {
    SoRenderCommand command;
    command.modelMatrix.setTranslate(SbVec3f(
      -0.9f + (i % columns) * dx, -0.9f + (i / columns) * dy,
      i >= litCount + texturedCount + coloredCount
        ? -static_cast<float>(i % 13) * 0.001f : 0.0f));
    command.geometry.topology = SO_TOPOLOGY_TRIANGLES;
    command.geometry.vertexCount = 4;
    command.geometry.normalCount = 4;
    command.geometry.indexCount = 6;
    command.geometry.positions = positions;
    command.geometry.normals = normals;
    command.geometry.indices = indices;
    command.geometry.vertexStride = sizeof(float) * 3;
    command.material.shadingModel = SO_SHADING_LEGACY_GOURAUD;
    command.material.diffuse = SbVec4f(0.65f, 0.72f, 0.85f, 1.0f);
    command.lightingHandle = lightingHandle;
    if (i < litCount) {
      command.geometry.resourceKey = 0x4645415455524501ULL;
    }
    else if (i < litCount + texturedCount) {
      command.geometry.resourceKey = 0x4645415455524502ULL;
      command.geometry.texcoords = texcoords;
      command.geometry.texcoordStride = sizeof(float) * 4;
      command.material.texture.pixels = texels;
      command.material.texture.width = 2;
      command.material.texture.height = 2;
      command.material.texture.numComponents = 4;
      command.material.texture.cacheKey = 0x4645415454580001ULL;
      command.material.texture.revision = 1;
    }
    else if (i < litCount + texturedCount + coloredCount) {
      command.geometry.resourceKey = 0x4645415455524503ULL;
      command.geometry.colors = colors;
    }
    else {
      command.geometry.resourceKey = 0x4645415455524504ULL;
      command.opacityClass = SO_OPACITY_TRANSPARENT;
      command.material.opacity = 0.55f;
      command.material.diffuse[3] = 0.55f;
      command.state.depth.writeEnabled = FALSE;
      command.state.blend.enabled = TRUE;
      command.state.blend.srcRGBFactor = SO_BLEND_FACTOR_SRC_ALPHA;
      command.state.blend.dstRGBFactor =
        SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      command.state.blend.srcAlphaFactor = SO_BLEND_FACTOR_ONE;
      command.state.blend.dstAlphaFactor =
        SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    drawlist.addCommand(command);
  }

  SoRenderParams params;
  params.viewport = SbViewportRegion(256, 256);
  params.viewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(256, 256));
  params.viewMatrix.makeIdentity();
  params.projMatrix.makeIdentity();
  params.clearColor.setValue(0.0f, 0.0f, 0.0f, 1.0f);
  params.flags = SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH;
  SoRenderPlanner planner;
  SoRenderPlan plan;
  planner.build(drawlist, params.viewMatrix, plan);
  SoGLRenderBackend backend;
  SoRenderBackendInitParams initParams;
  if (!backend.initialize(initParams)) {
    unavailable = "retained OpenGL backend initialization failed";
    return false;
  }
  backend.setPhaseTimingEnabled(TRUE);
  for (int warmup = 0; warmup < 5; ++warmup) {
    context.bindFramebuffer();
    backend.render(drawlist, plan, params);
  }
  glFinish();

  std::vector<double> cpu, gpu, completion, preparation, state, program, submit;
  GLuint query = 0;
  glGenQueries(1, &query);
  for (int sample = 0; sample < samples; ++sample) {
    context.bindFramebuffer();
    const Clock::time_point totalStart = Clock::now();
    glBeginQuery(GL_TIME_ELAPSED, query);
    const Clock::time_point cpuStart = Clock::now();
    backend.render(drawlist, plan, params);
    cpu.push_back(elapsedMs(cpuStart));
    const SoRenderStatistics statistics = backend.getRenderStatistics();
    preparation.push_back(statistics.commandPreparationNanoseconds / 1000000.0);
    state.push_back(statistics.stateSetupNanoseconds / 1000000.0);
    program.push_back(statistics.programBindingNanoseconds / 1000000.0);
    submit.push_back(statistics.drawSubmissionNanoseconds / 1000000.0);
    glEndQuery(GL_TIME_ELAPSED);
    GLuint64 nanoseconds = 0;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
    completion.push_back(elapsedMs(totalStart));
    gpu.push_back(static_cast<double>(nanoseconds) / 1000000.0);
  }
  glDeleteQueries(1, &query);
  const SoRenderStatistics statistics = backend.getRenderStatistics();
  const uint64_t checksum = checksumPixels(context.readPixels());
  const uint64_t expectedDrawCalls = 4;
  if (statistics.drawCalls != expectedDrawCalls ||
      statistics.instancedCommands != static_cast<uint64_t>(commandCount) ||
      statistics.instanceRejectedTexture != 0 ||
      statistics.instanceRejectedVertexAttributes != 0 ||
      statistics.instanceRejectedMaterial != 0 || checksum == 0) {
    unavailable = "feature-rich workload classification was incorrect";
    backend.shutdown();
    return false;
  }
  backend.shutdown();

  result.workload = "feature_rich_scene";
  result.renderer = "DrawList";
  result.profile = profile == GLTestProfile::Core ? "core" : "compatibility";
  result.semanticDraws = commandCount;
  result.samples = samples;
  result.cpuMedianMs = percentile(cpu, 0.5);
  result.cpuP95Ms = percentile(cpu, 0.95);
  result.gpuMedianMs = percentile(gpu, 0.5);
  result.gpuP95Ms = percentile(gpu, 0.95);
  result.completionMedianMs = percentile(completion, 0.5);
  result.completionP95Ms = percentile(completion, 0.95);
  result.commandPreparationMs = percentile(preparation, 0.5);
  result.stateSetupMs = percentile(state, 0.5);
  result.programBindingMs = percentile(program, 0.5);
  result.drawSubmissionMs = percentile(submit, 0.5);
  result.renderStatistics = statistics;
  result.pixelChecksum = checksum;
  return true;
}

bool runMixedRetainedScene(GLTestProfile profile, int commandCount, int samples,
                           Measurement & result, std::string & unavailable)
{
  GLTestContextConfig config;
  config.profile = profile;
  config.major = 3;
  config.minor = 3;
  config.width = 256;
  config.height = 256;
  GLTestContext context;
  if (!context.initialize(config) || !checkTimerQueries()) {
    unavailable = "required OpenGL context or timer queries are unavailable";
    return false;
  }
  const float quad[] = {
    -0.025f, -0.025f, 0.0f, 0.025f, -0.025f, 0.0f,
     0.025f,  0.025f, 0.0f, -0.025f, 0.025f, 0.0f
  };
  const uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };
  const int segments = commandCount >= 100 ? 5 : 2;
  const int perSegment = commandCount / segments;
  const int repeatedA = perSegment / 2;
  const int unique = perSegment / 5;
  const int repeatedB = perSegment / 5;
  const int transparent = perSegment - repeatedA - unique - repeatedB;
  std::vector<std::array<float, 12>> uniqueGeometry;
  uniqueGeometry.reserve(static_cast<size_t>(unique * segments));
  SoDrawList drawlist;
  drawlist.reserve(commandCount);
  const int columns = static_cast<int>(std::ceil(std::sqrt(
    static_cast<double>(commandCount))));
  const int rows = (commandCount + columns - 1) / columns;
  const float dx = 1.8f / static_cast<float>(std::max(1, columns - 1));
  const float dy = 1.8f / static_cast<float>(std::max(1, rows - 1));
  const auto append = [&](int index, uint64_t resourceKey,
                          const float * positions, bool translucent) {
    SoRenderCommand command;
    const float x = index == 0 ? 0.0f : -0.9f + (index % columns) * dx;
    const float y = index == 0 ? 0.0f : -0.9f + (index / columns) * dy;
    command.modelMatrix.setTranslate(SbVec3f(
      x, y, translucent ? -static_cast<float>(index % 11) * 0.002f : 0.0f));
    command.geometry.topology = SO_TOPOLOGY_TRIANGLES;
    command.geometry.vertexCount = 4;
    command.geometry.indexCount = 6;
    command.geometry.positions = positions;
    command.geometry.indices = indices;
    command.geometry.vertexStride = sizeof(float) * 3;
    command.geometry.resourceKey = resourceKey;
    command.geometry.resourceRevision = 1;
    const float shade = static_cast<float>((index * 29) % 101) / 100.0f;
    command.material.diffuse = SbVec4f(0.2f + 0.7f * shade,
                                       0.8f - 0.5f * shade,
                                       0.3f + 0.5f * shade,
                                       translucent ? 0.6f : 1.0f);
    command.objectId = static_cast<SoObjectId>(index + 1);
    command.nodeId = static_cast<SoNodeId>(index + 1001);
    command.instanceId = static_cast<SoInstanceId>(index + 2001);
    if (translucent) {
      command.opacityClass = SO_OPACITY_TRANSPARENT;
      command.material.opacity = 0.6f;
      command.state.depth.writeEnabled = FALSE;
      command.state.blend.enabled = TRUE;
      command.state.blend.srcRGBFactor = SO_BLEND_FACTOR_SRC_ALPHA;
      command.state.blend.dstRGBFactor = SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      command.state.blend.srcAlphaFactor = SO_BLEND_FACTOR_SRC_ALPHA;
      command.state.blend.dstAlphaFactor = SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    drawlist.addCommand(command);
  };
  int index = 0;
  for (int segment = 0; segment < segments; ++segment) {
    if (segment) {
      SoDepthClearEvent event;
      event.sequence = static_cast<uint32_t>(index);
      drawlist.addDepthClearEvent(event);
    }
    for (int i = 0; i < repeatedA; ++i, ++index)
      append(index, 0x4d495845440001ULL, quad, false);
    for (int i = 0; i < unique; ++i, ++index) {
      const float skew = static_cast<float>((index % 7) + 1) * 0.001f;
      uniqueGeometry.push_back({-0.025f, -0.025f, 0.0f,
        0.025f + skew, -0.025f, 0.0f, 0.025f, 0.025f, 0.0f,
        -0.025f, 0.025f + skew, 0.0f});
      append(index, 0x100000ULL + index, uniqueGeometry.back().data(), false);
    }
    for (int i = 0; i < repeatedB; ++i, ++index)
      append(index, 0x4d495845440002ULL, quad, false);
    for (int i = 0; i < transparent; ++i, ++index)
      append(index, 0x4d495845440003ULL, quad, true);
  }
  SoSelectionState selection;
  for (int selected = 0; selected < commandCount; selected += 10) {
    SoSelectionTarget target;
    target.commandIndex = selected;
    target.objectId = static_cast<SoObjectId>(selected + 1);
    target.nodeId = static_cast<SoNodeId>(selected + 1001);
    target.instanceId = static_cast<SoInstanceId>(selected + 2001);
    target.color = SbColor4f(1.0f, 0.8f, 0.0f, 0.65f);
    selection.selected.push_back(target);
  }
  SoRenderParams params;
  params.viewport = SbViewportRegion(256, 256);
  params.viewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(256, 256));
  params.viewMatrix.makeIdentity();
  params.projMatrix.makeIdentity();
  params.clearColor.setValue(0.0f, 0.0f, 0.0f, 1.0f);
  params.clearDepth = 1.0f;
  params.flags = SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH;
  SoRenderPlanner planner;
  SoRenderPlan plan;
  planner.build(drawlist, params.viewMatrix, plan);
  SoGLRenderBackend backend;
  SoRenderBackendInitParams initParams;
  if (!backend.initialize(initParams)) {
    unavailable = "retained OpenGL backend initialization failed";
    return false;
  }
  backend.setPhaseTimingEnabled(TRUE);
  for (int warmup = 0; warmup < 5; ++warmup) {
    context.bindFramebuffer();
    backend.render(drawlist, plan, params);
  }
  glFinish();
  std::vector<double> cpu, gpu, completion, preparation, state, program, submit;
  GLuint query = 0;
  glGenQueries(1, &query);
  for (int sample = 0; sample < samples; ++sample) {
    context.bindFramebuffer();
    const Clock::time_point totalStart = Clock::now();
    glBeginQuery(GL_TIME_ELAPSED, query);
    const Clock::time_point cpuStart = Clock::now();
    backend.render(drawlist, plan, params);
    cpu.push_back(elapsedMs(cpuStart));
    const SoRenderStatistics stats = backend.getRenderStatistics();
    preparation.push_back(stats.commandPreparationNanoseconds / 1000000.0);
    state.push_back(stats.stateSetupNanoseconds / 1000000.0);
    program.push_back(stats.programBindingNanoseconds / 1000000.0);
    submit.push_back(stats.drawSubmissionNanoseconds / 1000000.0);
    glEndQuery(GL_TIME_ELAPSED);
    GLuint64 nanoseconds = 0;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
    completion.push_back(elapsedMs(totalStart));
    gpu.push_back(static_cast<double>(nanoseconds) / 1000000.0);
  }
  glDeleteQueries(1, &query);
  SoRenderStatistics statistics = backend.getRenderStatistics();
  std::vector<double> selectionTimes, pickTimes, refreshTimes, mutationTimes;
  std::vector<double> pickUpdateCpuTimes, pickUpdateCompletionTimes;
  std::vector<double> idOnlyTimes, asyncIdSubmitTimes, asyncIdReadyTimes;
  for (int sample = 0; sample < samples; ++sample) {
    context.bindFramebuffer();
    backend.render(drawlist, plan, params);
    const Clock::time_point start = Clock::now();
    backend.renderSelection(drawlist, selection, params);
    selectionTimes.push_back(elapsedMs(start));
  }
  glFinish();
  context.bindFramebuffer();
  backend.updatePickBuffer(drawlist, plan, params);
  for (int sample = 0; sample < samples; ++sample) {
    SoPickResult pick;
    Clock::time_point start = Clock::now();
    backend.pickClosest(128, 128, 2, pick);
    pickTimes.push_back(elapsedMs(start));
    start = Clock::now();
    backend.updatePickBuffer(drawlist, plan, params);
    backend.pickClosest(128, 128, 2, pick);
    refreshTimes.push_back(elapsedMs(start));
  }
  for (int sample = 0; sample < samples; ++sample) {
    SoPickResult pick;
    Clock::time_point start = Clock::now();
    backend.pickClosest(128, 128, 2, SoPickReadbackMode::ID_ONLY, pick);
    idOnlyTimes.push_back(elapsedMs(start));
    SoAsyncPickRequest request;
    start = Clock::now();
    backend.requestPickClosestAsync(128, 128, 2,
                                    SoPickReadbackMode::ID_ONLY, request);
    asyncIdSubmitTimes.push_back(elapsedMs(start));
    SoAsyncPickStatus status = SoAsyncPickStatus::PENDING;
    while (status == SoAsyncPickStatus::PENDING) {
      const Clock::time_point pollStart = Clock::now();
      status = backend.pollPickClosestAsync(request, pick);
      result.asyncIdPollMaxMs = std::max(
        result.asyncIdPollMaxMs, elapsedMs(pollStart));
      if (status == SoAsyncPickStatus::PENDING) std::this_thread::yield();
    }
    if (status != SoAsyncPickStatus::HIT || pick.hasDepth) {
      unavailable = "ID-only asynchronous pick returned invalid coverage";
      backend.shutdown();
      return false;
    }
    asyncIdReadyTimes.push_back(elapsedMs(start));
  }
  for (int sample = 0; sample < samples; ++sample) {
    glFinish();
    const Clock::time_point completionStart = Clock::now();
    const Clock::time_point cpuStart = Clock::now();
    backend.updatePickBuffer(drawlist, plan, params);
    pickUpdateCpuTimes.push_back(elapsedMs(cpuStart));
    glFinish();
    pickUpdateCompletionTimes.push_back(elapsedMs(completionStart));
  }
  const SoRenderStatistics pickStatistics = backend.getRenderStatistics();
  statistics.pickDrawCalls = pickStatistics.pickDrawCalls;
  statistics.pickInstancedBatches = pickStatistics.pickInstancedBatches;
  statistics.pickInstancedEntries = pickStatistics.pickInstancedEntries;
  statistics.asyncPickBufferAllocations =
    pickStatistics.asyncPickBufferAllocations;
  for (int sample = 0; sample < samples; ++sample) {
    const uint64_t revision = static_cast<uint64_t>(sample + 2);
    for (int command = 0; command < drawlist.getNumCommands(); ++command) {
      SoRenderCommand & item = drawlist.getCommand(command);
      if (item.geometry.resourceKey == 0x4d495845440001ULL)
        item.geometry.resourceRevision = revision;
    }
    const Clock::time_point start = Clock::now();
    context.bindFramebuffer();
    backend.render(drawlist, plan, params);
    mutationTimes.push_back(elapsedMs(start));
  }
  context.bindFramebuffer();
  backend.render(drawlist, plan, params);
  const uint64_t checksum = checksumPixels(context.readPixels());
  if (statistics.drawCalls <= 1 ||
      statistics.drawCalls >= static_cast<uint64_t>(commandCount) ||
      statistics.instancedBatches <= 1 || statistics.maxInstanceBatchSize < 5 ||
      statistics.instanceBreakGeometryResource == 0 ||
      statistics.instanceBreakPlanBoundary == 0 ||
      statistics.pickDrawCalls >= static_cast<uint64_t>(commandCount) ||
      statistics.pickInstancedBatches <= 1 ||
      statistics.pickInstancedEntries == 0 ||
      checksum == 0) {
    unavailable = "mixed workload did not retain expected batch fragmentation";
    backend.shutdown();
    return false;
  }
  backend.shutdown();
  result.workload = "mixed_retained_scene";
  result.renderer = "DrawList";
  result.profile = profile == GLTestProfile::Core ? "core" : "compatibility";
  result.semanticDraws = commandCount;
  result.samples = samples;
  result.cpuMedianMs = percentile(cpu, 0.5);
  result.cpuP95Ms = percentile(cpu, 0.95);
  result.gpuMedianMs = percentile(gpu, 0.5);
  result.gpuP95Ms = percentile(gpu, 0.95);
  result.completionMedianMs = percentile(completion, 0.5);
  result.completionP95Ms = percentile(completion, 0.95);
  result.commandPreparationMs = percentile(preparation, 0.5);
  result.stateSetupMs = percentile(state, 0.5);
  result.programBindingMs = percentile(program, 0.5);
  result.drawSubmissionMs = percentile(submit, 0.5);
  result.selectionMedianMs = percentile(selectionTimes, 0.5);
  result.selectionP95Ms = percentile(selectionTimes, 0.95);
  result.pickMedianMs = percentile(pickTimes, 0.5);
  result.pickP95Ms = percentile(pickTimes, 0.95);
  result.refreshPickMs = percentile(refreshTimes, 0.5);
  result.pickUpdateCpuMedianMs = percentile(pickUpdateCpuTimes, 0.5);
  result.pickUpdateCompletionMedianMs = percentile(
    pickUpdateCompletionTimes, 0.5);
  result.pickIdOnlyMedianMs = percentile(idOnlyTimes, 0.5);
  result.asyncIdSubmitMedianMs = percentile(asyncIdSubmitTimes, 0.5);
  result.asyncIdReadyMedianMs = percentile(asyncIdReadyTimes, 0.5);
  result.mutationMedianMs = percentile(mutationTimes, 0.5);
  result.mutationP95Ms = percentile(mutationTimes, 0.95);
  result.renderStatistics = statistics;
  result.pixelChecksum = checksum;
  return true;
}

bool runIncrementalMutationScaling(GLTestProfile profile, int drawCount,
                                   int samples,
                                   std::vector<Measurement> & results,
                                   std::string & unavailable)
{
  GLTestContextConfig config;
  config.profile = profile;
  config.major = 3;
  config.minor = 3;
  config.width = 256;
  config.height = 256;
  GLTestContext context;
  if (!context.initialize(config)) {
    unavailable = "requested OpenGL context is unavailable";
    return false;
  }

  SceneMutationHandles mutations;
  SoOrthographicCamera * camera = NULL;
  SoSeparator * scene = makeScene(
    WorkloadKind::MaterialChurn, drawCount, camera, &mutations);
  if (mutations.transforms.size() != static_cast<size_t>(drawCount) ||
      mutations.materials.size() != static_cast<size_t>(drawCount) ||
      mutations.coordinates.size() != static_cast<size_t>(drawCount)) {
    unavailable = "mutation scene did not expose one target per occurrence";
    camera->unref();
    scene->unref();
    return false;
  }

  SbViewportRegion viewport(SbVec2s(256, 256));
  viewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(256, 256));
  SoRenderManager manager;
  manager.setViewportRegion(viewport);
  manager.setSceneGraph(scene);
  manager.setCamera(camera);
  manager.setLightingMode(SoRenderManager::UNLIT);
  manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
  manager.setRenderPhaseTimingEnabled(TRUE);
  for (int warmup = 0; warmup < 3; ++warmup) {
    context.bindFramebuffer();
    manager.render(TRUE, TRUE);
  }
  glFinish();

  const auto measure = [&](const char * name,
                           const std::function<void(int)> & mutate) {
    std::vector<double> cpuTimes;
    std::vector<double> constructionTimes;
    for (int sample = 0; sample < samples; ++sample) {
      mutate(sample);
      context.bindFramebuffer();
      const Clock::time_point start = Clock::now();
      manager.render(TRUE, TRUE);
      cpuTimes.push_back(elapsedMs(start));
      constructionTimes.push_back(
        manager.getRenderStatistics().drawListConstructionNanoseconds /
        1000000.0);
    }
    Measurement result;
    result.workload = std::string(name) + "_" + std::to_string(drawCount);
    result.renderer = "DrawList";
    result.profile = profile == GLTestProfile::Core
      ? "core" : "compatibility";
    result.semanticDraws = drawCount;
    result.samples = samples;
    result.cpuMedianMs = percentile(cpuTimes, 0.5);
    result.cpuP95Ms = percentile(cpuTimes, 0.95);
    result.drawListConstructionMs = percentile(constructionTimes, 0.5);
    result.renderStatistics = manager.getRenderStatistics();
    result.pixelChecksum = checksumPixels(context.readPixels());
    results.push_back(result);
  };

  measure("incremental_unchanged", [](int) {});
  measure("incremental_transform_1", [&](int sample) {
    mutations.transforms[0]->translation.setValue(
      0.001f * static_cast<float>((sample & 1) ? 1 : -1), 0.0f, 0.0f);
  });
  measure("incremental_material_1", [&](int sample) {
    mutations.materials[0]->diffuseColor.set1Value(
      0, SbColor((sample & 1) ? 0.7f : 0.2f, 0.4f, 0.6f));
  });
  measure("incremental_transparency_1", [&](int sample) {
    mutations.materials[0]->transparency.set1Value(
      0, (sample & 1) ? 0.25f : 0.0f);
  });
  measure("incremental_geometry_1", [&](int sample) {
    mutations.coordinates[0]->point.set1Value(
      0, SbVec3f((sample & 1) ? -0.40f : -0.44f, -0.42f, 0.0f));
  });

  const int sharedCounts[] = { 1, 10, 100, 1000, 10000 };
  for (size_t countIndex = 0;
       countIndex < sizeof(sharedCounts) / sizeof(sharedCounts[0]);
       ++countIndex) {
    const int sharedCommandCount = sharedCounts[countIndex];
    if (sharedCommandCount > drawCount) continue;

    SoSeparator * sharedScene = new SoSeparator;
    SoSeparator * sharedContainer = new SoSeparator;
    SoSeparator * sharedBranch = new SoSeparator;
    SoCoordinate3 * sharedCoordinates = new SoCoordinate3;
    const SbVec3f sharedTriangle[] = {
      SbVec3f(-0.42f, -0.42f, 0.0f), SbVec3f(0.42f, -0.42f, 0.0f),
      SbVec3f(0.0f, 0.42f, 0.0f)
    };
    sharedCoordinates->point.setValues(0, 3, sharedTriangle);
    sharedBranch->addChild(sharedCoordinates);
    for (int i = 0; i < sharedCommandCount; ++i) {
      SoFaceSet * face = new SoFaceSet;
      face->numVertices.set1Value(0, 3);
      sharedBranch->addChild(face);
    }
    sharedContainer->addChild(sharedBranch);
    sharedScene->addChild(sharedContainer);
    sharedScene->ref();
    manager.setSceneGraph(sharedScene);
    context.bindFramebuffer();
    manager.render(TRUE, TRUE);

    std::vector<double> cpuTimes;
    std::vector<double> constructionTimes;
    for (int sample = 0; sample < samples; ++sample) {
      sharedCoordinates->point.set1Value(
        0, SbVec3f((sample & 1) ? -0.40f : -0.44f, -0.42f, 0.0f));
      context.bindFramebuffer();
      const Clock::time_point start = Clock::now();
      manager.render(TRUE, TRUE);
      cpuTimes.push_back(elapsedMs(start));
      constructionTimes.push_back(
        manager.getRenderStatistics().drawListConstructionNanoseconds /
        1000000.0);
    }
    Measurement sharedResult;
    sharedResult.workload = "incremental_geometry_shared_" +
      std::to_string(sharedCommandCount);
    sharedResult.renderer = "DrawList";
    sharedResult.profile = profile == GLTestProfile::Core
      ? "core" : "compatibility";
    sharedResult.semanticDraws = sharedCommandCount;
    sharedResult.samples = samples;
    sharedResult.cpuMedianMs = percentile(cpuTimes, 0.5);
    sharedResult.cpuP95Ms = percentile(cpuTimes, 0.95);
    sharedResult.drawListConstructionMs = percentile(constructionTimes, 0.5);
    sharedResult.renderStatistics = manager.getRenderStatistics();
    sharedResult.pixelChecksum = checksumPixels(context.readPixels());
    results.push_back(sharedResult);

    manager.setSceneGraph(NULL);
    sharedScene->unref();
  }

  for (size_t countIndex = 0;
       countIndex < sizeof(sharedCounts) / sizeof(sharedCounts[0]);
       ++countIndex) {
    const int sharedCommandCount = sharedCounts[countIndex];
    if (sharedCommandCount > drawCount) continue;

    SoSeparator * recipeScene = new SoSeparator;
    SoSeparator * recipeContainer = new SoSeparator;
    SoSeparator * recipeBranch = new SoSeparator;
    SoCoordinate3 * recipeCoordinates = new SoCoordinate3;
    const SbVec3f recipeTriangle[] = {
      SbVec3f(-0.42f, -0.42f, 0.0f), SbVec3f(0.42f, -0.42f, 0.0f),
      SbVec3f(0.0f, 0.42f, 0.0f)
    };
    recipeCoordinates->point.setValues(0, 3, recipeTriangle);
    recipeBranch->addChild(recipeCoordinates);
    SoFaceSet * recipeFace = new SoFaceSet;
    recipeFace->numVertices.set1Value(0, 3);
    for (int i = 0; i < sharedCommandCount; ++i) {
      recipeBranch->addChild(recipeFace);
    }
    recipeContainer->addChild(recipeBranch);
    recipeScene->addChild(recipeContainer);
    recipeScene->ref();
    manager.setSceneGraph(recipeScene);
    context.bindFramebuffer();
    manager.render(TRUE, TRUE);

    std::vector<double> cpuTimes;
    std::vector<double> constructionTimes;
    for (int sample = 0; sample < samples; ++sample) {
      recipeCoordinates->point.set1Value(
        0, SbVec3f((sample & 1) ? -0.40f : -0.44f, -0.42f, 0.0f));
      context.bindFramebuffer();
      const Clock::time_point start = Clock::now();
      manager.render(TRUE, TRUE);
      cpuTimes.push_back(elapsedMs(start));
      constructionTimes.push_back(
        manager.getRenderStatistics().drawListConstructionNanoseconds /
        1000000.0);
    }
    Measurement recipeResult;
    recipeResult.workload = "incremental_geometry_shared_recipe_" +
      std::to_string(sharedCommandCount);
    recipeResult.renderer = "DrawList";
    recipeResult.profile = profile == GLTestProfile::Core
      ? "core" : "compatibility";
    recipeResult.semanticDraws = sharedCommandCount;
    recipeResult.samples = samples;
    recipeResult.cpuMedianMs = percentile(cpuTimes, 0.5);
    recipeResult.cpuP95Ms = percentile(cpuTimes, 0.95);
    recipeResult.drawListConstructionMs = percentile(constructionTimes, 0.5);
    recipeResult.renderStatistics = manager.getRenderStatistics();
    recipeResult.pixelChecksum = checksumPixels(context.readPixels());
    results.push_back(recipeResult);

    manager.setSceneGraph(NULL);
    recipeScene->unref();
  }

  manager.releaseRenderBackendResources();
  manager.setCamera(NULL);
  manager.setSceneGraph(NULL);
  camera->unref();
  scene->unref();
  return true;
}

bool runAssemblyMutations(GLTestProfile profile, WorkloadKind workload,
                          int occurrenceCount, int samples,
                          std::vector<Measurement> & results,
                          std::string & unavailable)
{
  GLTestContextConfig config;
  config.profile = profile;
  config.major = 3;
  config.minor = 3;
  config.width = 256;
  config.height = 256;
  GLTestContext context;
  if (!context.initialize(config)) {
    unavailable = "requested OpenGL context is unavailable";
    return false;
  }

  SceneMutationHandles mutations;
  SoOrthographicCamera * camera = nullptr;
  SoSeparator * scene = makeScene(workload, occurrenceCount, camera, &mutations);
  const int definitionCount = assemblyDefinitionCount(occurrenceCount);
  const int firstDefinitionOccurrences = std::min(occurrenceCount,
    (occurrenceCount + definitionCount - 1) / definitionCount);
  if (mutations.transforms.size() != static_cast<size_t>(occurrenceCount) ||
      mutations.materials.size() != static_cast<size_t>(occurrenceCount) ||
      mutations.coordinates.size() != static_cast<size_t>(occurrenceCount) ||
      mutations.definitionCoordinates.size() !=
        static_cast<size_t>(definitionCount)) {
    unavailable = "assembly scene did not expose its mutation targets";
    camera->unref();
    scene->unref();
    return false;
  }

  SbViewportRegion viewport(SbVec2s(256, 256));
  viewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(256, 256));
  SoRenderManager manager;
  manager.setViewportRegion(viewport);
  manager.setSceneGraph(scene);
  manager.setCamera(camera);
  manager.setLightingMode(SoRenderManager::UNLIT);
  manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
  manager.setRenderPhaseTimingEnabled(TRUE);
  context.bindFramebuffer();
  manager.render(TRUE, TRUE);

  bool valid = true;
  const auto measure = [&](const char * suffix, uint64_t expectedUpdates,
                           const std::function<void(int)> & mutate) {
    std::vector<double> frameTimes;
    std::vector<double> constructionTimes;
    for (int sample = 0; sample < samples; ++sample) {
      mutate(sample);
      context.bindFramebuffer();
      const Clock::time_point start = Clock::now();
      manager.render(TRUE, TRUE);
      frameTimes.push_back(elapsedMs(start));
      const SoRenderStatistics statistics = manager.getRenderStatistics();
      constructionTimes.push_back(
        statistics.drawListConstructionNanoseconds / 1000000.0);
      if (statistics.drawListRebuilds != 0 ||
          statistics.incrementalCommandUpdates != expectedUpdates) {
        std::ostringstream reason;
        reason << workloadName(workload) << ' ' << suffix
               << " updated " << statistics.incrementalCommandUpdates
               << " commands with " << statistics.drawListRebuilds
               << " rebuilds; expected " << expectedUpdates
               << " incremental updates";
        unavailable = reason.str();
        valid = false;
        return;
      }
    }
    Measurement result;
    result.workload = std::string(workloadName(workload)) + '_' + suffix +
      '_' + std::to_string(occurrenceCount);
    result.renderer = "DrawList";
    result.profile = profile == GLTestProfile::Core
      ? "core" : "compatibility";
    result.semanticDraws = occurrenceCount;
    result.samples = samples;
    result.cpuMedianMs = percentile(frameTimes, 0.5);
    result.cpuP95Ms = percentile(frameTimes, 0.95);
    result.mutationMedianMs = result.cpuMedianMs;
    result.mutationP95Ms = result.cpuP95Ms;
    result.drawListConstructionMs = percentile(constructionTimes, 0.5);
    result.renderStatistics = manager.getRenderStatistics();
    result.pixelChecksum = checksumPixels(context.readPixels());
    results.push_back(result);
  };

  measure("placement_1", 2, [&](int sample) {
    mutations.transforms[0]->translation.setValue(
      (sample & 1) ? -0.02f : 0.02f, 0.0f, 0.0f);
  });
  if (valid) measure("material_1", 1, [&](int sample) {
    mutations.materials[0]->diffuseColor.setValue(
      (sample & 1) ? SbColor(0.8f, 0.3f, 0.2f)
                   : SbColor(0.2f, 0.6f, 0.8f));
  });
  if (valid) {
    SoCoordinate3 * geometryTarget =
      workload == WorkloadKind::SharedAssemblyExpanded
      ? mutations.coordinates[0] : mutations.definitionCoordinates[0];
    const uint64_t expectedGeometryUpdates =
      workload == WorkloadKind::SharedAssemblyExpanded
      ? 2 : static_cast<uint64_t>(firstDefinitionOccurrences) * 2;
    measure("geometry_definition_1", expectedGeometryUpdates,
      [&](int sample) {
        geometryTarget->point.set1Value(0,
          SbVec3f((sample & 1) ? -0.10f : -0.06f, -0.06f, 0.0f));
      });
  }

  manager.releaseRenderBackendResources();
  manager.setCamera(nullptr);
  manager.setSceneGraph(nullptr);
  camera->unref();
  scene->unref();
  return valid;
}

Options parseOptions(int argc, char ** argv)
{
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--smoke") options.smoke = true;
    else if (arg == "--samples" && i + 1 < argc) options.samples = std::atoi(argv[++i]);
    else if (arg == "--rebuild-only" && i + 1 < argc)
      options.rebuildOnly = std::atoi(argv[++i]);
    else if (arg == "--incremental-only" && i + 1 < argc)
      options.incrementalOnly = std::atoi(argv[++i]);
    else if (arg == "--assembly-only" && i + 1 < argc)
      options.assemblyOnly = std::atoi(argv[++i]);
    else if (arg == "--output" && i + 1 < argc) options.output = argv[++i];
    else {
      std::cerr << "Usage: CoinRenderGLBenchmarks [--smoke] [--samples N] "
                   "[--rebuild-only N] [--incremental-only N] "
                   "[--assembly-only N] "
                   "[--output FILE]\n";
      std::exit(2);
    }
  }
  return options;
}

std::string toJson(const std::vector<Measurement> & results,
                   const std::vector<std::string> & unavailable,
                   const Options & options)
{
  std::ostringstream out;
  out << std::fixed << std::setprecision(6);
  out << "{\n  \"schema_version\": 1,\n  \"mode\": \""
      << (options.smoke ? "smoke" : "benchmark")
      << "\",\n  \"time_unit\": \"ms\",\n  \"benchmarks\": [\n";
  for (size_t i = 0; i < results.size(); ++i) {
    const Measurement & r = results[i];
    out << "    {\"workload\": \"" << r.workload
        << "\", \"renderer\": \"" << r.renderer
        << "\", \"profile\": \"" << r.profile
        << "\", \"semantic_draws\": " << r.semanticDraws
        << ", \"samples\": " << r.samples
        << ", \"cpu_render_median_ms\": " << r.cpuMedianMs
        << ", \"cpu_render_p95_ms\": " << r.cpuP95Ms
        << ", \"gpu_median_ms\": " << r.gpuMedianMs
        << ", \"gpu_p95_ms\": " << r.gpuP95Ms
        << ", \"completion_median_ms\": " << r.completionMedianMs
        << ", \"completion_p95_ms\": " << r.completionP95Ms
        << ", \"drawlist_construction_ms\": "
        << r.drawListConstructionMs
        << ", \"plan_construction_ms\": " << r.planConstructionMs
        << ", \"cold_pick_ms\": " << r.coldPickMs
        << ", \"refresh_pick_ms\": " << r.refreshPickMs
        << ", \"async_pick_submit_ms\": " << r.asyncPickSubmitMs
        << ", \"async_pick_ready_ms\": " << r.asyncPickReadyMs
        << ", \"async_pick_poll_max_ms\": " << r.asyncPickPollMaxMs
        << ", \"selection_median_ms\": " << r.selectionMedianMs
        << ", \"selection_p95_ms\": " << r.selectionP95Ms
        << ", \"mutation_median_ms\": " << r.mutationMedianMs
        << ", \"mutation_p95_ms\": " << r.mutationP95Ms
        << ", \"pick_update_cpu_median_ms\": "
        << r.pickUpdateCpuMedianMs
        << ", \"pick_update_completion_median_ms\": "
        << r.pickUpdateCompletionMedianMs
        << ", \"pick_id_only_median_ms\": " << r.pickIdOnlyMedianMs
        << ", \"async_id_submit_median_ms\": "
        << r.asyncIdSubmitMedianMs
        << ", \"async_id_ready_median_ms\": " << r.asyncIdReadyMedianMs
        << ", \"async_id_poll_max_ms\": " << r.asyncIdPollMaxMs
        << ", \"draw_calls\": " << r.renderStatistics.drawCalls
        << ", \"program_binds\": " << r.renderStatistics.programBinds
        << ", \"skipped_program_binds\": "
        << r.renderStatistics.skippedProgramBinds
        << ", \"viewport_changes\": "
        << r.renderStatistics.viewportChanges
        << ", \"skipped_viewport_changes\": "
        << r.renderStatistics.skippedViewportChanges
        << ", \"frame_matrix_uploads\": "
        << r.renderStatistics.frameMatrixUploads
        << ", \"skipped_frame_matrix_uploads\": "
        << r.renderStatistics.skippedFrameMatrixUploads
        << ", \"material_uniform_batches\": "
        << r.renderStatistics.materialUniformBatches
        << ", \"skipped_material_uniform_batches\": "
        << r.renderStatistics.skippedMaterialUniformBatches
        << ", \"state_changes\": "
        << r.renderStatistics.stateChanges
        << ", \"skipped_state_changes\": "
        << r.renderStatistics.skippedStateChanges
        << ", \"vertex_array_binds\": "
        << r.renderStatistics.vertexArrayBinds
        << ", \"skipped_vertex_array_binds\": "
        << r.renderStatistics.skippedVertexArrayBinds
        << ", \"drawlist_rebuilds\": "
        << r.renderStatistics.drawListRebuilds
        << ", \"retained_commands\": "
        << r.renderStatistics.retainedCommands
        << ", \"retained_geometry_resources\": "
        << r.renderStatistics.retainedGeometryResources
        << ", \"retained_path_commands\": "
        << r.renderStatistics.retainedPathCommands
        << ", \"retained_unique_paths\": "
        << r.renderStatistics.retainedUniquePaths
        << ", \"retained_reused_paths\": "
        << r.renderStatistics.retainedReusedPaths
        << ", \"retained_path_node_entries\": "
        << r.renderStatistics.retainedPathNodeEntries
        << ", \"retained_path_node_references\": "
        << r.renderStatistics.retainedPathNodeReferences
        << ", \"retained_path_storage_bytes\": "
        << r.renderStatistics.retainedPathStorageBytes
        << ", \"retained_dependency_branches\": "
        << r.renderStatistics.retainedDependencyBranches
        << ", \"retained_dependency_command_references\": "
        << r.renderStatistics.retainedDependencyCommandReferences
        << ", \"retained_dependency_storage_bytes\": "
        << r.renderStatistics.retainedDependencyStorageBytes
        << ", \"incremental_command_updates\": "
        << r.renderStatistics.incrementalCommandUpdates
        << ", \"instanced_batches\": "
        << r.renderStatistics.instancedBatches
        << ", \"instanced_commands\": "
        << r.renderStatistics.instancedCommands
        << ", \"draw_calls_avoided\": "
        << r.renderStatistics.drawCallsAvoided
        << ", \"instance_bytes_uploaded\": "
        << r.renderStatistics.instanceBytesUploaded
        << ", \"instance_batches_2_to_4\": "
        << r.renderStatistics.instanceBatches2To4
        << ", \"instance_batches_5_to_16\": "
        << r.renderStatistics.instanceBatches5To16
        << ", \"instance_batches_17_to_64\": "
        << r.renderStatistics.instanceBatches17To64
        << ", \"instance_batches_65_plus\": "
        << r.renderStatistics.instanceBatches65Plus
        << ", \"max_instance_batch_size\": "
        << r.renderStatistics.maxInstanceBatchSize
        << ", \"instance_rejected_geometry\": "
        << r.renderStatistics.instanceRejectedGeometry
        << ", \"instance_rejected_vertex_attributes\": "
        << r.renderStatistics.instanceRejectedVertexAttributes
        << ", \"instance_rejected_material\": "
        << r.renderStatistics.instanceRejectedMaterial
        << ", \"instance_rejected_texture\": "
        << r.renderStatistics.instanceRejectedTexture
        << ", \"instance_rejected_render_state\": "
        << r.renderStatistics.instanceRejectedRenderState
        << ", \"instance_break_geometry_resource\": "
        << r.renderStatistics.instanceBreakGeometryResource
        << ", \"instance_break_material\": "
        << r.renderStatistics.instanceBreakMaterial
        << ", \"instance_break_render_state\": "
        << r.renderStatistics.instanceBreakRenderState
        << ", \"instance_break_plan_boundary\": "
        << r.renderStatistics.instanceBreakPlanBoundary
        << ", \"pick_draw_calls\": "
        << r.renderStatistics.pickDrawCalls
        << ", \"pick_instanced_batches\": "
        << r.renderStatistics.pickInstancedBatches
        << ", \"pick_instanced_entries\": "
        << r.renderStatistics.pickInstancedEntries
        << ", \"async_pick_buffer_allocations\": "
        << r.renderStatistics.asyncPickBufferAllocations
        << ", \"command_preparation_ms\": "
        << r.commandPreparationMs
        << ", \"state_setup_ms\": "
        << r.stateSetupMs
        << ", \"program_binding_ms\": "
        << r.programBindingMs
        << ", \"draw_submission_ms\": "
        << r.drawSubmissionMs
        << ", \"pick_median_ms\": " << r.pickMedianMs
        << ", \"pick_p95_ms\": " << r.pickP95Ms
        << ", \"pixel_checksum\": " << r.pixelChecksum << "}";
    if (i + 1 != results.size()) out << ',';
    out << '\n';
  }
  out << "  ],\n  \"unavailable\": [";
  for (size_t i = 0; i < unavailable.size(); ++i) {
    if (i) out << ", ";
    out << '\"' << unavailable[i] << '\"';
  }
  out << "]\n}\n";
  return out.str();
}

} // namespace

int main(int argc, char ** argv)
{
  const Options options = parseOptions(argc, argv);
  SoDB::init();
  const int samples = options.samples > 0 ? options.samples : (options.smoke ? 2 : 30);
  const int draws = options.smoke ? 8 : 500;
  const WorkloadKind workloads[] = {
    WorkloadKind::ManyDraws,
    WorkloadKind::MaterialChurn,
    WorkloadKind::Transparency,
    WorkloadKind::DensePicking,
    WorkloadKind::FeatureRich
  };
  std::vector<Measurement> results;
  std::vector<std::string> unavailable;
  const auto runAssemblyVariants = [&](int occurrenceCount) {
    const WorkloadKind assemblyWorkloads[] = {
      WorkloadKind::SharedAssemblyExpanded,
      WorkloadKind::SharedAssemblySources,
      WorkloadKind::SharedAssemblyRecipe
    };
    for (WorkloadKind workload : assemblyWorkloads) {
#if COIN_HAVE_LEGACY_GL_RENDERER
      Measurement legacy;
      std::string legacyReason;
      if (runVariant(GLTestProfile::Compatibility,
                     SoRenderManager::RenderPipeline::LEGACY_GL,
                     "LegacyGL", workload, occurrenceCount, samples,
                     legacy, legacyReason)) {
        results.push_back(legacy);
      }
      else unavailable.push_back(std::string(workloadName(workload)) +
        ":LegacyGL: " + legacyReason);
#endif
      const GLTestProfile profiles[] = {
        GLTestProfile::Compatibility, GLTestProfile::Core
      };
      for (GLTestProfile profile : profiles) {
        Measurement retained;
        std::string reason;
        if (runVariant(profile, SoRenderManager::RenderPipeline::DRAW_LIST,
                       "DrawList", workload, occurrenceCount, samples,
                       retained, reason, true)) {
          results.push_back(retained);
        }
        else unavailable.push_back(std::string(workloadName(workload)) +
          ":DrawList " +
          (profile == GLTestProfile::Core ? "core: " : "compatibility: ") +
          reason);
        std::string mutationReason;
        if (!runAssemblyMutations(profile, workload, occurrenceCount, samples,
                                  results, mutationReason)) {
          unavailable.push_back(std::string(workloadName(workload)) +
            ":mutations DrawList " +
            (profile == GLTestProfile::Core ? "core: " : "compatibility: ") +
            mutationReason);
        }
      }
    }
  };
  if (options.incrementalOnly > 0) {
    std::string reason;
    if (!runIncrementalMutationScaling(
          GLTestProfile::Core, options.incrementalOnly, samples,
          results, reason)) {
      unavailable.push_back("incremental_mutation_scaling:DrawList core: " +
                            reason);
    }
    const std::string document = toJson(results, unavailable, options);
    if (options.output.empty()) std::cout << document;
    else {
      std::ofstream output(options.output.c_str());
      if (!output) return 1;
      output << document;
    }
    SoDB::finish();
    return results.empty() ? 77 : 0;
  }
  if (options.assemblyOnly > 0) {
    runAssemblyVariants(options.assemblyOnly);
    const std::string document = toJson(results, unavailable, options);
    if (options.output.empty()) std::cout << document;
    else {
      std::ofstream output(options.output.c_str());
      if (!output) return 1;
      output << document;
    }
    SoDB::finish();
    return results.empty() ? 77 : 0;
  }
  if (options.rebuildOnly > 0) {
    Measurement rebuild;
    std::string reason;
    if (runVariant(GLTestProfile::Core,
                   SoRenderManager::RenderPipeline::DRAW_LIST,
                   "DrawList", WorkloadKind::FeatureRich,
                   options.rebuildOnly, samples, rebuild, reason, true)) {
      rebuild.workload = "feature_rich_rebuild_" +
        std::to_string(options.rebuildOnly);
      results.push_back(rebuild);
    }
    else unavailable.push_back("feature_rich_rebuild_" +
      std::to_string(options.rebuildOnly) + ":DrawList core: " + reason);
    const std::string document = toJson(results, unavailable, options);
    if (options.output.empty()) std::cout << document;
    else {
      std::ofstream output(options.output.c_str());
      if (!output) return 1;
      output << document;
    }
    SoDB::finish();
    return results.empty() ? 77 : 0;
  }
  for (size_t i = 0; i < sizeof(workloads) / sizeof(workloads[0]); ++i) {
#if COIN_HAVE_LEGACY_GL_RENDERER
    Measurement legacy;
    std::string reason;
    if (runVariant(GLTestProfile::Compatibility,
                   SoRenderManager::RenderPipeline::LEGACY_GL,
                   "LegacyGL", workloads[i], draws, samples, legacy, reason)) {
      results.push_back(legacy);
    }
    else unavailable.push_back(std::string(workloadName(workloads[i])) +
      ":LegacyGL: " + reason);
#endif
    Measurement compatibility;
    std::string compatReason;
    if (runVariant(GLTestProfile::Compatibility,
                   SoRenderManager::RenderPipeline::DRAW_LIST,
                   "DrawList", workloads[i], draws, samples,
                   compatibility, compatReason)) {
      results.push_back(compatibility);
    }
    else unavailable.push_back(std::string(workloadName(workloads[i])) +
      ":DrawList compatibility: " + compatReason);

    Measurement core;
    std::string coreReason;
    if (runVariant(GLTestProfile::Core,
                   SoRenderManager::RenderPipeline::DRAW_LIST,
                   "DrawList", workloads[i], draws, samples, core, coreReason)) {
      results.push_back(core);
    }
    else unavailable.push_back(std::string(workloadName(workloads[i])) +
      ":DrawList core: " + coreReason);
  }
  runAssemblyVariants(options.smoke ? 24 : 500);
  const int rebuildCounts[] = {
    options.smoke ? 40 : 500,
    options.smoke ? 0 : 5000,
    options.smoke ? 0 : 50000
  };
  const int rebuildSamples = options.smoke ? samples : std::min(samples, 10);
  for (size_t i = 0;
       i < sizeof(rebuildCounts) / sizeof(rebuildCounts[0]); ++i) {
    if (rebuildCounts[i] == 0) continue;
    Measurement rebuild;
    std::string reason;
    if (runVariant(GLTestProfile::Core,
                   SoRenderManager::RenderPipeline::DRAW_LIST,
                   "DrawList", WorkloadKind::FeatureRich,
                   rebuildCounts[i], rebuildSamples, rebuild, reason, true)) {
      rebuild.workload = "feature_rich_rebuild_" +
        std::to_string(rebuildCounts[i]);
      results.push_back(rebuild);
    }
    else {
      unavailable.push_back("feature_rich_rebuild_" +
        std::to_string(rebuildCounts[i]) + ":DrawList core: " + reason);
    }
  }
  const int indexedCounts[] = { 1, options.smoke ? 8 : 100,
                                options.smoke ? 0 : 500 };
  for (size_t i = 0; i < sizeof(indexedCounts) / sizeof(indexedCounts[0]); ++i) {
    if (indexedCounts[i] == 0) continue;
    const GLTestProfile profiles[] = {
      GLTestProfile::Compatibility, GLTestProfile::Core
    };
    for (size_t p = 0; p < sizeof(profiles) / sizeof(profiles[0]); ++p) {
      for (const bool lines : { false, true }) {
        Measurement indexed;
        std::string reason;
        if (runIndexedInstances(profiles[p], indexedCounts[i], samples,
                                indexed, reason, lines)) {
          results.push_back(indexed);
        }
        else {
          unavailable.push_back(std::string(lines
              ? "indexed_line_instances_" : "indexed_instances_") +
            std::to_string(indexedCounts[i]) + ":DrawList " +
            (profiles[p] == GLTestProfile::Core
              ? "core: " : "compatibility: ") + reason);
        }
      }
    }
  }
  const GLTestProfile mixedProfiles[] = {
    GLTestProfile::Compatibility, GLTestProfile::Core
  };
  for (size_t i = 0;
       i < sizeof(mixedProfiles) / sizeof(mixedProfiles[0]); ++i) {
    Measurement featureRich;
    std::string featureReason;
    if (runFeatureRichScene(mixedProfiles[i], options.smoke ? 40 : 500,
                            samples, featureRich, featureReason)) {
      results.push_back(featureRich);
    }
    else {
      unavailable.push_back(std::string("feature_rich_scene:DrawList ") +
        (mixedProfiles[i] == GLTestProfile::Core ? "core: " :
                                                   "compatibility: ") +
        featureReason);
    }
    Measurement mixed;
    std::string reason;
    if (runMixedRetainedScene(mixedProfiles[i], options.smoke ? 40 : 500,
                              samples, mixed, reason)) {
      results.push_back(mixed);
    }
    else {
      unavailable.push_back(std::string("mixed_retained_scene:DrawList ") +
        (mixedProfiles[i] == GLTestProfile::Core ? "core: " :
                                                   "compatibility: ") +
        reason);
    }
  }
  const std::string document = toJson(results, unavailable, options);
  if (options.output.empty()) std::cout << document;
  else {
    std::ofstream output(options.output.c_str());
    if (!output) return 1;
    output << document;
  }
  SoDB::finish();
  return results.empty() ? 77 : 0;
}
