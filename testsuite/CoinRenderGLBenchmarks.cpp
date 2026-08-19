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
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

enum class WorkloadKind { ManyDraws, MaterialChurn, Transparency, DensePicking };

struct Options {
  bool smoke = false;
  int samples = 0;
  std::string output;
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
  }
  return "unknown";
}

SoSeparator * makeScene(WorkloadKind kind, int drawCount,
                        SoOrthographicCamera *& camera)
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
  lightModel->model = SoLightModel::BASE_COLOR;
  root->addChild(lightModel);
  SoMaterial * defaultMaterial = new SoMaterial;
  defaultMaterial->diffuseColor.setValue(0.3f, 0.7f, 1.0f);
  root->addChild(defaultMaterial);

  const SbVec3f triangle[] = {
    SbVec3f(-0.42f, -0.42f, 0.0f),
    SbVec3f(0.42f, -0.42f, 0.0f),
    SbVec3f(0.0f, 0.42f, 0.0f)
  };
  const int columns = static_cast<int>(std::ceil(std::sqrt(
    static_cast<double>(drawCount))));
  const int rows = (drawCount + columns - 1) / columns;
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

    if (kind != WorkloadKind::ManyDraws) {
      SoMaterial * material = new SoMaterial;
      const float value = static_cast<float>((i * 17) % 101) / 100.0f;
      material->diffuseColor.setValue(0.2f + value * 0.8f,
                                      0.9f - value * 0.7f,
                                      0.3f + value * 0.5f);
      if (kind == WorkloadKind::Transparency) material->transparency = 0.35f;
      draw->addChild(material);
    }
    SoCoordinate3 * coordinates = new SoCoordinate3;
    coordinates->point.setValues(0, 3, triangle);
    SoFaceSet * face = new SoFaceSet;
    face->numVertices.set1Value(0, 3);
    draw->addChild(coordinates);
    draw->addChild(face);
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
  manager.setLightingMode(SoRenderManager::UNLIT);
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
    manager.render(TRUE, TRUE);
    cpu.push_back(elapsedMs(cpuStart));
    const SoRenderStatistics sampleStatistics = manager.getRenderStatistics();
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
    if (renderStatistics.instancedCommands !=
          static_cast<uint64_t>(drawCount) ||
        renderStatistics.drawCalls != 1) {
      std::cerr << "FAIL: " << renderer << ' ' << workloadName(workload)
                << " did not collapse compatible commands into one batch\n";
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
                         Measurement & result, std::string & unavailable)
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
  const uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };
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
    command.geometry.topology = SO_TOPOLOGY_TRIANGLES;
    command.geometry.vertexCount = 4;
    command.geometry.indexCount = 6;
    command.geometry.positions = positions;
    command.geometry.indices = indices;
    command.geometry.vertexStride = sizeof(float) * 3;
    command.geometry.cacheKey = 0x494e5354414e4345ULL;
    command.geometry.revision = 1;
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
  const bool expectedBatch = instanceCount > 1;
  if (statistics.drawCalls != 1 ||
      (expectedBatch && (statistics.instancedCommands !=
                           static_cast<uint64_t>(instanceCount) ||
                         statistics.drawCallsAvoided !=
                           static_cast<uint64_t>(instanceCount - 1))) ||
      pixelChecksum == 0) {
    std::cerr << "FAIL: indexed instance workload did not render as one "
                 "correct batch\n";
    backend.shutdown();
    return false;
  }
  backend.shutdown();

  result.workload = "indexed_instances_" + std::to_string(instanceCount);
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
  const int transparentLitCount =
    commandCount - litCount - texturedCount - coloredCount;
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
  const uint64_t expectedDrawCalls = static_cast<uint64_t>(
    1 + texturedCount + coloredCount + transparentLitCount);
  if (statistics.drawCalls != expectedDrawCalls ||
      statistics.instancedCommands != static_cast<uint64_t>(litCount) ||
      statistics.instanceRejectedTexture !=
        static_cast<uint64_t>(texturedCount) ||
      statistics.instanceRejectedVertexAttributes !=
        static_cast<uint64_t>(coloredCount) ||
      statistics.instanceRejectedMaterial !=
        static_cast<uint64_t>(transparentLitCount) || checksum == 0) {
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

Options parseOptions(int argc, char ** argv)
{
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--smoke") options.smoke = true;
    else if (arg == "--samples" && i + 1 < argc) options.samples = std::atoi(argv[++i]);
    else if (arg == "--output" && i + 1 < argc) options.output = argv[++i];
    else {
      std::cerr << "Usage: CoinRenderGLBenchmarks [--smoke] [--samples N] "
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
    WorkloadKind::DensePicking
  };
  std::vector<Measurement> results;
  std::vector<std::string> unavailable;
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
  const int indexedCounts[] = { 1, options.smoke ? 8 : 100,
                                options.smoke ? 0 : 500 };
  for (size_t i = 0; i < sizeof(indexedCounts) / sizeof(indexedCounts[0]); ++i) {
    if (indexedCounts[i] == 0) continue;
    const GLTestProfile profiles[] = {
      GLTestProfile::Compatibility, GLTestProfile::Core
    };
    for (size_t p = 0; p < sizeof(profiles) / sizeof(profiles[0]); ++p) {
      Measurement indexed;
      std::string reason;
      if (runIndexedInstances(profiles[p], indexedCounts[i], samples,
                              indexed, reason)) {
        results.push_back(indexed);
      }
      else {
        unavailable.push_back("indexed_instances_" +
          std::to_string(indexedCounts[i]) + ":DrawList " +
          (profiles[p] == GLTestProfile::Core ? "core: " : "compatibility: ") +
          reason);
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
