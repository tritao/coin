#include "support/GLTestContext.h"
#include "support/RenderWorkloads.h"
#include "rendering/SoGLRenderBackend.h"
#include "rendering/SoRenderPlan.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/lists/SoPickedPointList.h>
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
#include <unordered_set>
#include <vector>

namespace {

using namespace coin_test;
using Clock = std::chrono::steady_clock;

struct Options {
  bool smoke = false;
  bool phaseTiming = true;
  int samples = 0;
  int rebuildOnly = 0;
  int incrementalOnly = 0;
  int assemblyOnly = 0;
  int assemblyRebuildOnly = 0;
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
  double drawListConstructionMs = 0.0;
  double traversalUnattributedMs = 0.0;
  double commandPathIdentityMs = 0.0;
  double commandStateMs = 0.0;
  double geometryResourceMs = 0.0;
  double drawListAppendMs = 0.0;
  double pathDependencyMs = 0.0;
  double primitiveGenerationMs = 0.0;
  double geometryPackingMs = 0.0;
  double commandEmissionMs = 0.0;
  double commandGeometryIdentityMs = 0.0;
  double commandStateCaptureMs = 0.0;
  double commandFinalizationMs = 0.0;
  double commandPickingMetadataMs = 0.0;
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
  double depthStackMedianMs = 0.0;
  double depthStackP95Ms = 0.0;
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
                std::string & unavailable, bool forceDrawListRebuild = false,
                bool phaseTiming = true)
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
    phaseTiming && pipeline == SoRenderManager::RenderPipeline::DRAW_LIST);
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
  std::vector<double> traversalUnattributed;
  std::vector<double> commandPathIdentity;
  std::vector<double> commandState;
  std::vector<double> geometryResource;
  std::vector<double> drawListAppend;
  std::vector<double> pathDependency;
  std::vector<double> primitiveGeneration;
  std::vector<double> geometryPacking;
  std::vector<double> commandEmission;
  std::vector<double> commandGeometryIdentity;
  std::vector<double> commandStateCapture;
  std::vector<double> commandFinalization;
  std::vector<double> commandPickingMetadata;
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
    commandPathIdentity.push_back(
      sampleStatistics.drawListCommandPathIdentityNanoseconds / 1000000.0);
    commandState.push_back(
      sampleStatistics.drawListCommandStateNanoseconds / 1000000.0);
    geometryResource.push_back(
      sampleStatistics.drawListGeometryResourceNanoseconds / 1000000.0);
    drawListAppend.push_back(
      sampleStatistics.drawListAppendNanoseconds / 1000000.0);
    pathDependency.push_back(
      sampleStatistics.drawListPathDependencyNanoseconds / 1000000.0);
    primitiveGeneration.push_back(
      sampleStatistics.drawListPrimitiveGenerationNanoseconds / 1000000.0);
    geometryPacking.push_back(
      sampleStatistics.drawListGeometryPackingNanoseconds / 1000000.0);
    commandEmission.push_back(
      sampleStatistics.drawListCommandEmissionNanoseconds / 1000000.0);
    commandGeometryIdentity.push_back(
      sampleStatistics.drawListCommandGeometryIdentityNanoseconds / 1000000.0);
    commandStateCapture.push_back(
      sampleStatistics.drawListCommandStateCaptureNanoseconds / 1000000.0);
    commandFinalization.push_back(
      sampleStatistics.drawListCommandFinalizationNanoseconds / 1000000.0);
    commandPickingMetadata.push_back(
      sampleStatistics.drawListCommandPickingMetadataNanoseconds / 1000000.0);
    const uint64_t attributedConstruction =
      sampleStatistics.drawListPrimitiveGenerationNanoseconds +
      sampleStatistics.drawListGeometryPackingNanoseconds +
      sampleStatistics.drawListCommandEmissionNanoseconds;
    traversalUnattributed.push_back(
      sampleStatistics.drawListConstructionNanoseconds > attributedConstruction
        ? (sampleStatistics.drawListConstructionNanoseconds -
           attributedConstruction) / 1000000.0
        : 0.0);
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
  result.traversalUnattributedMs = percentile(traversalUnattributed, 0.5);
  result.commandPathIdentityMs = percentile(commandPathIdentity, 0.5);
  result.commandStateMs = percentile(commandState, 0.5);
  result.geometryResourceMs = percentile(geometryResource, 0.5);
  result.drawListAppendMs = percentile(drawListAppend, 0.5);
  result.pathDependencyMs = percentile(pathDependency, 0.5);
  result.primitiveGenerationMs = percentile(primitiveGeneration, 0.5);
  result.geometryPackingMs = percentile(geometryPacking, 0.5);
  result.commandEmissionMs = percentile(commandEmission, 0.5);
  result.commandGeometryIdentityMs = percentile(commandGeometryIdentity, 0.5);
  result.commandStateCaptureMs = percentile(commandStateCapture, 0.5);
  result.commandFinalizationMs = percentile(commandFinalization, 0.5);
  result.commandPickingMetadataMs = percentile(commandPickingMetadata, 0.5);
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
    std::vector<double> planTimes;
    for (int sample = 0; sample < samples; ++sample) {
      mutate(sample);
      context.bindFramebuffer();
      const Clock::time_point start = Clock::now();
      manager.render(TRUE, TRUE);
      cpuTimes.push_back(elapsedMs(start));
      const SoRenderStatistics statistics = manager.getRenderStatistics();
      if (statistics.drawListRebuilds != 0) {
        std::cerr << "FAIL: " << name
                  << " unexpectedly rebuilt the retained draw list\n";
        std::exit(1);
      }
      const bool unchanged = std::string(name) == "incremental_unchanged";
      if (unchanged != (statistics.planConstructionNanoseconds == 0)) {
        std::cerr << "FAIL: " << name
                  << (unchanged ? " rebuilt" : " reused")
                  << " the render plan unexpectedly\n";
        std::exit(1);
      }
      constructionTimes.push_back(
        statistics.drawListConstructionNanoseconds / 1000000.0);
      planTimes.push_back(
        statistics.planConstructionNanoseconds / 1000000.0);
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
    result.planConstructionMs = percentile(planTimes, 0.5);
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

bool runAssemblyInteractions(GLTestProfile profile, int occurrenceCount,
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
  SoOrthographicCamera * camera = nullptr;
  SoSeparator * scene = makeScene(
    WorkloadKind::SharedAssemblyRecipe, occurrenceCount, camera, &mutations);
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
  context.bindFramebuffer();
  manager.render(TRUE, TRUE);
  const SoRenderStatistics baseline = manager.getRenderStatistics();
  const uint64_t baselineChecksum = checksumPixels(context.readPixels());
  const uint64_t maximumResources = static_cast<uint64_t>(
    assemblyDefinitionCount(occurrenceCount)) * 3;
  if (baseline.retainedCommands != static_cast<uint64_t>(occurrenceCount * 2) ||
      baseline.retainedGeometryResources > maximumResources) {
    unavailable = "assembly interaction scene retained unexpected structure";
    manager.setCamera(nullptr);
    manager.setSceneGraph(nullptr);
    camera->unref();
    scene->unref();
    return false;
  }

  const auto cursorForOccurrence = [&](int occurrence) {
    const int columns = static_cast<int>(std::ceil(std::sqrt(
      static_cast<double>(occurrenceCount))));
    const float x = (static_cast<float>(occurrence % columns) -
      columns * 0.5f) * 0.65f;
    const float y = (static_cast<float>(occurrence / columns) -
      columns * 0.5f) * 0.65f;
    const float halfHeight = std::max(4.0f, camera->height.getValue() * 0.5f);
    const int px = static_cast<int>(std::lround(
      (x / halfHeight * 0.5f + 0.5f) * 255.0f));
    const int py = static_cast<int>(std::lround(
      (y / halfHeight * 0.5f + 0.5f) * 255.0f));
    return SbVec2s(static_cast<short>(px), static_cast<short>(py));
  };

  Measurement hover;
  hover.workload = "shared_assembly_hover_pick_" +
    std::to_string(occurrenceCount);
  hover.renderer = "DrawList";
  hover.profile = profile == GLTestProfile::Core ? "core" : "compatibility";
  hover.semanticDraws = occurrenceCount * 2;
  hover.samples = samples;
  std::vector<double> pickTimes;
  std::vector<double> asyncSubmitTimes;
  std::vector<double> asyncReadyTimes;
  std::unordered_set<SoInstanceId> identities;
  const SbVec2s coldCursor = cursorForOccurrence(0);
  SoPickedPoint * picked = nullptr;
  Clock::time_point start = Clock::now();
  if (!manager.pickClosest(coldCursor[0], coldCursor[1], 3, picked) || !picked) {
    unavailable = "assembly cold hover pick returned no scene hit";
    manager.setCamera(nullptr);
    manager.setSceneGraph(nullptr);
    camera->unref();
    scene->unref();
    return false;
  }
  hover.coldPickMs = elapsedMs(start);
  delete picked;
  for (int sample = 0; sample < samples; ++sample) {
    const int occurrence = (sample * 7919) % occurrenceCount;
    const SbVec2s cursor = cursorForOccurrence(occurrence);
    picked = nullptr;
    start = Clock::now();
    if (!manager.pickClosest(cursor[0], cursor[1], 3, picked) || !picked) {
      unavailable = "assembly warm hover pick returned no scene hit";
      break;
    }
    pickTimes.push_back(elapsedMs(start));
    delete picked;

    SoAsyncPickRequest request;
    start = Clock::now();
    if (!manager.requestPickIdentityAsync(cursor[0], cursor[1], 3, request)) {
      unavailable = "assembly asynchronous identity pick was rejected";
      break;
    }
    asyncSubmitTimes.push_back(elapsedMs(start));
    SoPickIdentity identity;
    SoAsyncPickStatus status = SoAsyncPickStatus::PENDING;
    while (status == SoAsyncPickStatus::PENDING) {
      const Clock::time_point pollStart = Clock::now();
      status = manager.pollPickIdentityAsync(request, identity);
      hover.asyncIdPollMaxMs = std::max(
        hover.asyncIdPollMaxMs, elapsedMs(pollStart));
      if (status == SoAsyncPickStatus::PENDING) std::this_thread::yield();
      if (elapsedMs(start) > 1000.0) break;
    }
    asyncReadyTimes.push_back(elapsedMs(start));
    if (status != SoAsyncPickStatus::HIT || identity.instanceId == 0) {
      unavailable = "assembly asynchronous identity pick did not resolve";
      break;
    }
    identities.insert(identity.instanceId);
  }
  if (pickTimes.size() != static_cast<size_t>(samples)) {
    manager.releaseRenderBackendResources();
    manager.setCamera(nullptr);
    manager.setSceneGraph(nullptr);
    camera->unref();
    scene->unref();
    return false;
  }
  hover.pickMedianMs = percentile(pickTimes, 0.5);
  hover.pickP95Ms = percentile(pickTimes, 0.95);
  hover.asyncIdSubmitMedianMs = percentile(asyncSubmitTimes, 0.5);
  hover.asyncIdReadyMedianMs = percentile(asyncReadyTimes, 0.5);
  hover.renderStatistics = manager.getRenderStatistics();
  if (hover.renderStatistics.drawListRebuilds != 0 ||
      hover.renderStatistics.retainedGeometryResources > maximumResources ||
      hover.renderStatistics.pickDrawCalls > maximumResources ||
      hover.renderStatistics.pickInstancedEntries !=
        static_cast<uint64_t>(occurrenceCount * 2) ||
      hover.renderStatistics.asyncPickBufferAllocations > 3) {
    unavailable = "assembly hover picking violated retained batching invariants";
    manager.releaseRenderBackendResources();
    manager.setCamera(nullptr);
    manager.setSceneGraph(nullptr);
    camera->unref();
    scene->unref();
    return false;
  }
  context.bindFramebuffer();
  hover.pixelChecksum = checksumPixels(context.readPixels());
  results.push_back(hover);

  const auto measureSelection = [&](const char * name, int selectedCount,
                                    bool highlighted, bool churn,
                                    bool subelement = false) {
    std::vector<double> times;
    for (int sample = 0; sample < samples; ++sample) {
      SoSelectionState selection;
      const int offset = churn ? sample : 0;
      for (int selected = 0; selected < selectedCount; ++selected) {
        const int occurrence = (selected * 7919 + offset) % occurrenceCount;
        for (int commandOffset = 0; commandOffset < 2; ++commandOffset) {
          SoSelectionTarget target;
          target.commandIndex = occurrence * 2 + commandOffset;
          if (subelement) {
            target.type = commandOffset == 0 ? SO_PICK_FACE : SO_PICK_EDGE;
            target.elementIndex = selected % 8;
          }
          target.color = highlighted
            ? SbColor4f(0.2f, 0.8f, 1.0f, 0.75f)
            : SbColor4f(1.0f, 0.8f, 0.0f, 0.65f);
          if (highlighted) selection.highlighted.push_back(target);
          else selection.selected.push_back(target);
        }
      }
      manager.setSelectionState(selection);
      context.bindFramebuffer();
      const Clock::time_point renderStart = Clock::now();
      manager.render(TRUE, TRUE);
      times.push_back(elapsedMs(renderStart));
      const SoRenderStatistics statistics = manager.getRenderStatistics();
      if (statistics.drawListRebuilds != 0 ||
          statistics.retainedCommands !=
            static_cast<uint64_t>(occurrenceCount * 2) ||
          statistics.retainedGeometryResources > maximumResources ||
          statistics.selectionOverlayDrawCalls == 0 ||
          statistics.selectionOverlayDrawCalls >
            static_cast<uint64_t>(selectedCount * 2) ||
          (highlighted
            ? statistics.highlightedOverlayEntries
            : statistics.selectedOverlayEntries) !=
              static_cast<uint64_t>(selectedCount * 2) ||
          (!highlighted && selectedCount >= occurrenceCount / 10 &&
           occurrenceCount >= 100 &&
           statistics.selectionInstancedEntries == 0)) {
        std::ostringstream reason;
        reason << name << " changed retained assembly structure"
               << " (overlay-draws=" << statistics.selectionOverlayDrawCalls
               << ", instanced=" << statistics.selectionInstancedEntries
               << ", selected=" << statistics.selectedOverlayEntries
               << ", highlighted=" << statistics.highlightedOverlayEntries
               << ", candidates="
               << statistics.selectionPrimitiveCandidates
               << ", amplification="
               << statistics.selectionPrimitiveAmplification
               << ", rejected="
               << statistics.selectionPrimitiveBatchesRejected << ')';
        unavailable = reason.str();
        return false;
      }
    }
    Measurement measurement;
    measurement.workload = std::string(name) + '_' +
      std::to_string(occurrenceCount);
    measurement.renderer = "DrawList";
    measurement.profile = profile == GLTestProfile::Core
      ? "core" : "compatibility";
    measurement.semanticDraws = occurrenceCount * 2;
    measurement.samples = samples;
    measurement.selectionMedianMs = percentile(times, 0.5);
    measurement.selectionP95Ms = percentile(times, 0.95);
    measurement.cpuMedianMs = measurement.selectionMedianMs;
    measurement.cpuP95Ms = measurement.selectionP95Ms;
    measurement.renderStatistics = manager.getRenderStatistics();
    measurement.pixelChecksum = checksumPixels(context.readPixels());
    if (measurement.pixelChecksum == 0 ||
        measurement.pixelChecksum == baselineChecksum) {
      unavailable = std::string(name) +
        " did not produce a visible interaction overlay";
      return false;
    }
    results.push_back(measurement);
    return true;
  };

  const int onePercent = std::max(1, occurrenceCount / 100);
  const int tenPercent = std::max(1, occurrenceCount / 10);
  bool valid = identities.size() >= static_cast<size_t>(std::min(samples, 2));
  if (!valid) unavailable = "assembly hover picks did not preserve occurrence identity";
  if (valid) valid = measureSelection(
    "shared_assembly_selection_1_percent", onePercent, false, false);
  if (valid) valid = measureSelection(
    "shared_assembly_selection_10_percent", tenPercent, false, false);
  if (valid) valid = measureSelection(
    "shared_assembly_selection_churn", tenPercent, false, true);
  if (valid) valid = measureSelection(
    "shared_assembly_subelement_selection", tenPercent, false, true, true);
  if (valid) valid = measureSelection(
    "shared_assembly_preselection", 1, true, true);

  manager.setSelectionState(SoSelectionState());
  if (valid) {
    for (int occurrence = 0; occurrence < occurrenceCount; ++occurrence) {
      mutations.transforms[static_cast<size_t>(occurrence)]->translation
        .setValue(0.0f, 0.0f, -0.01f * static_cast<float>(occurrence));
    }
    context.bindFramebuffer();
    manager.render(TRUE, TRUE);
    context.bindFramebuffer();
    manager.render(TRUE, TRUE);
    std::vector<double> depthStackTimes;
    const int maxLayers = std::min(8, occurrenceCount);
    for (int sample = 0; sample < samples; ++sample) {
      SoPickedPointList stack;
      const Clock::time_point stackStart = Clock::now();
      if (!manager.pickDepthStack(128, 128, 3, maxLayers, stack, 32) ||
          stack.getLength() < std::min(2, occurrenceCount)) {
        unavailable = "assembly depth stack did not return overlapping hits";
        valid = false;
        break;
      }
      depthStackTimes.push_back(elapsedMs(stackStart));
    }
    if (valid) {
      Measurement depthStack;
      depthStack.workload = "shared_assembly_depth_stack_" +
        std::to_string(occurrenceCount);
      depthStack.renderer = "DrawList";
      depthStack.profile = profile == GLTestProfile::Core
        ? "core" : "compatibility";
      depthStack.semanticDraws = occurrenceCount * 2;
      depthStack.samples = samples;
      depthStack.depthStackMedianMs = percentile(depthStackTimes, 0.5);
      depthStack.depthStackP95Ms = percentile(depthStackTimes, 0.95);
      depthStack.cpuMedianMs = depthStack.depthStackMedianMs;
      depthStack.cpuP95Ms = depthStack.depthStackP95Ms;
      depthStack.renderStatistics = manager.getRenderStatistics();
      const uint64_t maximumDepthDraws = maximumResources *
        static_cast<uint64_t>(maxLayers + 1);
      if (depthStack.renderStatistics.drawListRebuilds != 0 ||
          depthStack.renderStatistics.depthStackDrawCalls > maximumDepthDraws ||
          depthStack.renderStatistics.depthStackInstancedEntries == 0) {
        unavailable = "assembly depth stack violated batching invariants";
        valid = false;
      }
      else results.push_back(depthStack);
    }
  }
  manager.releaseRenderBackendResources();
  manager.setCamera(nullptr);
  manager.setSceneGraph(nullptr);
  camera->unref();
  scene->unref();
  return valid;
}

bool runSubelementSelectionCurve(GLTestProfile profile, int primitiveCount,
                                 int commandCount, bool sharedGeometry,
                                 bool selectionChurn, int samples,
                                 Measurement & result,
                                 std::string & unavailable)
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

  const int columns = static_cast<int>(std::ceil(std::sqrt(
    static_cast<double>(commandCount))));
  const float radius = std::min(0.04f, 0.35f / columns);
  std::vector<float> positions(static_cast<size_t>(primitiveCount + 2) * 3);
  positions[0] = positions[1] = positions[2] = 0.0f;
  for (int vertex = 0; vertex <= primitiveCount; ++vertex) {
    const float angle = static_cast<float>(vertex) * 6.28318530718f /
      static_cast<float>(primitiveCount);
    const size_t offset = static_cast<size_t>(vertex + 1) * 3;
    positions[offset] = std::cos(angle) * radius;
    positions[offset + 1] = std::sin(angle) * radius;
    positions[offset + 2] = 0.0f;
  }
  std::vector<uint32_t> indices(static_cast<size_t>(primitiveCount) * 3);
  for (int primitive = 0; primitive < primitiveCount; ++primitive) {
    indices[static_cast<size_t>(primitive) * 3] = 0;
    indices[static_cast<size_t>(primitive) * 3 + 1] = primitive + 1;
    indices[static_cast<size_t>(primitive) * 3 + 2] = primitive + 2;
  }

  SoDrawList drawlist;
  drawlist.reserve(commandCount);
  SoSelectionState selection;
  selection.revision = 1;
  for (int commandIndex = 0; commandIndex < commandCount; ++commandIndex) {
    SoRenderCommand command;
    command.geometry.topology = SO_TOPOLOGY_TRIANGLES;
    command.geometry.positions = positions.data();
    command.geometry.vertexCount = primitiveCount + 2;
    command.geometry.vertexStride = sizeof(float) * 3;
    command.geometry.indices = indices.data();
    command.geometry.indexCount = static_cast<uint32_t>(indices.size());
    command.geometry.resourceKey = sharedGeometry
      ? 0x53454c4355525645ULL
      : 0x53454c0000000000ULL + static_cast<uint64_t>(commandIndex + 1);
    command.geometry.resourceRevision = 1;
    command.material.diffuse = SbVec4f(0.3f, 0.6f, 0.8f, 1.0f);
    const int column = commandIndex % columns;
    const int row = commandIndex / columns;
    const float spacing = 1.8f / columns;
    command.modelMatrix.setTranslate(SbVec3f(
      -0.9f + (column + 0.5f) * spacing,
      -0.9f + (row + 0.5f) * spacing, 0.0f));
    SoRenderElementRange range;
    range.type = SO_PICK_FACE;
    range.elementIndex = 0;
    range.drawStart = 0;
    range.drawCount = 3;
    command.pick.elementRanges.push_back(range);
    drawlist.addCommand(command);
    SoSelectionTarget target;
    target.commandIndex = commandIndex;
    target.type = SO_PICK_FACE;
    target.elementIndex = 0;
    target.color = SbColor4f(1.0f, 0.8f, 0.0f, 0.65f);
    selection.selected.push_back(target);
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

  std::vector<double> cpuTimes;
  std::vector<double> gpuTimes;
  uint64_t coldScratchGrowths = 0;
  uint64_t coldInstanceGrowths = 0;
  GLuint query = 0;
  glGenQueries(1, &query);
  for (int sample = 0; sample < samples; ++sample) {
    context.bindFramebuffer();
    backend.render(drawlist, plan, params);
    if (selectionChurn && !selection.selected.empty()) {
      const size_t rotation = static_cast<size_t>(sample + 1) %
        selection.selected.size();
      std::rotate(selection.selected.begin(),
                  selection.selected.begin() + rotation,
                  selection.selected.end());
      ++selection.revision;
    }
    glBeginQuery(GL_TIME_ELAPSED, query);
    const Clock::time_point start = Clock::now();
    backend.renderSelection(drawlist, selection, params);
    if (sample == 0) {
      const SoRenderStatistics coldStatistics = backend.getRenderStatistics();
      coldScratchGrowths = coldStatistics.selectionScratchCapacityGrowths;
      coldInstanceGrowths = coldStatistics.selectionInstanceCapacityGrowths;
    }
    cpuTimes.push_back(elapsedMs(start));
    glEndQuery(GL_TIME_ELAPSED);
    GLuint64 nanoseconds = 0;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
    gpuTimes.push_back(static_cast<double>(nanoseconds) / 1000000.0);
  }
  glDeleteQueries(1, &query);
  const SoRenderStatistics statistics = backend.getRenderStatistics();
  const uint64_t pixelChecksum = checksumPixels(context.readPixels());
  const uint64_t primitiveAmplification =
    static_cast<uint64_t>(primitiveCount) * commandCount;
  const uint64_t primitiveBudget = 256 + 64 * (commandCount - 1);
  const bool shouldBatch = sharedGeometry &&
    primitiveAmplification <= primitiveBudget;
  const bool valid = pixelChecksum != 0 &&
    coldScratchGrowths > 0 &&
    (samples < 2 || statistics.selectionScratchCapacityGrowths == 0) &&
    (!shouldBatch || coldInstanceGrowths > 0) &&
    (samples < 2 || statistics.selectionInstanceCapacityGrowths == 0) &&
    (samples < 2 || (selectionChurn
      ? statistics.selectionPlanCacheMisses == 1 &&
          statistics.selectionPlanCacheHits == 0
      : statistics.selectionPlanCacheHits == 1 &&
          statistics.selectionPlanCacheMisses == 0 &&
          statistics.selectionPlanReusedEntries == commandCount)) &&
    statistics.selectedOverlayEntries == commandCount &&
    (shouldBatch
      ? statistics.selectionInstancedEntries == commandCount &&
        statistics.selectionOverlayDrawCalls == 1
      : statistics.selectionInstancedEntries == 0 &&
        statistics.selectionOverlayDrawCalls == commandCount) &&
    (!sharedGeometry || shouldBatch ||
      statistics.selectionPrimitiveBatchesRejected == 1);
  if (!valid) {
    unavailable = "subelement selection complexity policy was inconsistent";
    backend.shutdown();
    return false;
  }

  result.workload = std::string("subelement_selection_") +
    (sharedGeometry ? "shared_" : "explicit_") +
    std::to_string(primitiveCount) + "_targets_" +
    std::to_string(commandCount) + (selectionChurn ? "_churn" : "");
  result.renderer = "DrawList";
  result.profile = profile == GLTestProfile::Core ? "core" : "compatibility";
  result.semanticDraws = commandCount;
  result.samples = samples;
  result.selectionMedianMs = percentile(cpuTimes, 0.5);
  result.selectionP95Ms = percentile(cpuTimes, 0.95);
  result.gpuMedianMs = percentile(gpuTimes, 0.5);
  result.gpuP95Ms = percentile(gpuTimes, 0.95);
  result.renderStatistics = statistics;
  result.pixelChecksum = pixelChecksum;
  backend.shutdown();
  return true;
}

Options parseOptions(int argc, char ** argv)
{
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--smoke") options.smoke = true;
    else if (arg == "--no-phase-timing") options.phaseTiming = false;
    else if (arg == "--samples" && i + 1 < argc) options.samples = std::atoi(argv[++i]);
    else if (arg == "--rebuild-only" && i + 1 < argc)
      options.rebuildOnly = std::atoi(argv[++i]);
    else if (arg == "--incremental-only" && i + 1 < argc)
      options.incrementalOnly = std::atoi(argv[++i]);
    else if (arg == "--assembly-only" && i + 1 < argc)
      options.assemblyOnly = std::atoi(argv[++i]);
    else if (arg == "--assembly-rebuild-only" && i + 1 < argc)
      options.assemblyRebuildOnly = std::atoi(argv[++i]);
    else if (arg == "--output" && i + 1 < argc) options.output = argv[++i];
    else {
      std::cerr << "Usage: CoinRenderGLBenchmarks [--smoke] [--samples N] "
                   "[--no-phase-timing] "
                   "[--rebuild-only N] [--incremental-only N] "
                   "[--assembly-only N] "
                   "[--assembly-rebuild-only N] "
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
      << "\",\n  \"time_unit\": \"ms\",\n  \"phase_timing\": "
      << (options.phaseTiming ? "true" : "false")
      << ",\n  \"benchmarks\": [\n";
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
        << ", \"traversal_unattributed_ms\": "
        << r.traversalUnattributedMs
        << ", \"command_path_identity_ms\": " << r.commandPathIdentityMs
        << ", \"command_state_ms\": " << r.commandStateMs
        << ", \"geometry_resource_ms\": " << r.geometryResourceMs
        << ", \"drawlist_append_ms\": " << r.drawListAppendMs
        << ", \"path_dependency_ms\": " << r.pathDependencyMs
        << ", \"primitive_generation_ms\": " << r.primitiveGenerationMs
        << ", \"geometry_packing_ms\": " << r.geometryPackingMs
        << ", \"command_emission_ms\": " << r.commandEmissionMs
        << ", \"command_geometry_identity_ms\": "
        << r.commandGeometryIdentityMs
        << ", \"command_state_capture_ms\": " << r.commandStateCaptureMs
        << ", \"command_finalization_ms\": " << r.commandFinalizationMs
        << ", \"command_picking_metadata_ms\": "
        << r.commandPickingMetadataMs
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
        << ", \"geometry_recipe_lookup_attempts\": "
        << r.renderStatistics.geometryRecipeLookupAttempts
        << ", \"geometry_recipe_cache_hits\": "
        << r.renderStatistics.geometryRecipeCacheHits
        << ", \"geometry_recipe_hash_lookups\": "
        << r.renderStatistics.geometryRecipeHashLookups
        << ", \"geometry_recipe_candidates_scanned\": "
        << r.renderStatistics.geometryRecipeCandidatesScanned
        << ", \"geometry_source_lookup_attempts\": "
        << r.renderStatistics.geometrySourceLookupAttempts
        << ", \"geometry_source_cache_hits\": "
        << r.renderStatistics.geometrySourceCacheHits
        << ", \"geometry_source_hash_lookups\": "
        << r.renderStatistics.geometrySourceHashLookups
        << ", \"geometry_source_candidates_scanned\": "
        << r.renderStatistics.geometrySourceCandidatesScanned
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
        << ", \"depth_stack_draw_calls\": "
        << r.renderStatistics.depthStackDrawCalls
        << ", \"depth_stack_instanced_batches\": "
        << r.renderStatistics.depthStackInstancedBatches
        << ", \"depth_stack_instanced_entries\": "
        << r.renderStatistics.depthStackInstancedEntries
        << ", \"selection_overlay_draw_calls\": "
        << r.renderStatistics.selectionOverlayDrawCalls
        << ", \"selection_instanced_batches\": "
        << r.renderStatistics.selectionInstancedBatches
        << ", \"selection_instanced_entries\": "
        << r.renderStatistics.selectionInstancedEntries
        << ", \"selected_overlay_entries\": "
        << r.renderStatistics.selectedOverlayEntries
        << ", \"highlighted_overlay_entries\": "
        << r.renderStatistics.highlightedOverlayEntries
        << ", \"selection_planned_batches\": "
        << r.renderStatistics.selectionPlannedBatches
        << ", \"selection_explicit_entries\": "
        << r.renderStatistics.selectionExplicitEntries
        << ", \"selection_planning_ms\": "
        << r.renderStatistics.selectionPlanningNanoseconds / 1000000.0
        << ", \"selection_scratch_capacity_growths\": "
        << r.renderStatistics.selectionScratchCapacityGrowths
        << ", \"selection_scratch_capacity_bytes\": "
        << r.renderStatistics.selectionScratchCapacityBytes
        << ", \"selection_plan_cache_hits\": "
        << r.renderStatistics.selectionPlanCacheHits
        << ", \"selection_plan_cache_misses\": "
        << r.renderStatistics.selectionPlanCacheMisses
        << ", \"selection_plan_reused_entries\": "
        << r.renderStatistics.selectionPlanReusedEntries
        << ", \"selection_instance_build_ms\": "
        << r.renderStatistics.selectionInstanceBuildNanoseconds / 1000000.0
        << ", \"selection_instance_upload_ms\": "
        << r.renderStatistics.selectionInstanceUploadNanoseconds / 1000000.0
        << ", \"selection_instance_capacity_growths\": "
        << r.renderStatistics.selectionInstanceCapacityGrowths
        << ", \"selection_instance_capacity_bytes\": "
        << r.renderStatistics.selectionInstanceCapacityBytes
        << ", \"selection_instance_bytes_uploaded\": "
        << r.renderStatistics.selectionInstanceBytesUploaded
        << ", \"selection_primitive_candidates\": "
        << r.renderStatistics.selectionPrimitiveCandidates
        << ", \"selection_primitive_batches_rejected\": "
        << r.renderStatistics.selectionPrimitiveBatchesRejected
        << ", \"selection_primitive_amplification\": "
        << r.renderStatistics.selectionPrimitiveAmplification
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
        << ", \"depth_stack_median_ms\": " << r.depthStackMedianMs
        << ", \"depth_stack_p95_ms\": " << r.depthStackP95Ms
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
        if (workload == WorkloadKind::SharedAssemblyRecipe) {
          std::string interactionReason;
          if (!runAssemblyInteractions(profile, occurrenceCount, samples,
                                       results, interactionReason)) {
            unavailable.push_back("shared_assembly_interactions:DrawList " +
              std::string(profile == GLTestProfile::Core
                ? "core: " : "compatibility: ") + interactionReason);
          }
        }
      }
    }
  };
  const auto runSelectionCurves = [&]() {
    const int primitiveCounts[] = {
      8, options.smoke ? 0 : 64, options.smoke ? 0 : 1000,
      options.smoke ? 0 : 10000
    };
    const GLTestProfile profiles[] = {
      GLTestProfile::Compatibility, GLTestProfile::Core
    };
    const int targetCounts[] = {
      10, options.smoke ? 0 : 100,
      options.smoke ? 0 : 1000, options.smoke ? 0 : 10000
    };
    for (GLTestProfile profile : profiles) {
      for (const int primitiveCount : primitiveCounts) {
        if (primitiveCount == 0) continue;
        for (const bool sharedGeometry : {false, true}) {
          Measurement curve;
          std::string reason;
          const int curveSamples = std::min(samples, options.smoke ? 2 : 10);
          if (runSubelementSelectionCurve(
                profile, primitiveCount, 40, sharedGeometry, false,
                curveSamples,
                curve, reason)) {
            results.push_back(curve);
          }
          else {
            unavailable.push_back(
              std::string("subelement_selection_curve:DrawList ") +
              (profile == GLTestProfile::Core ? "core: " :
                                                "compatibility: ") + reason);
          }
        }
      }
      for (const int targetCount : targetCounts) {
        if (targetCount == 0) continue;
        for (const bool sharedGeometry : {false, true}) {
          Measurement curve;
          std::string reason;
          const int curveSamples = std::min(
            samples, targetCount >= 1000 ? 3 : 10);
          if (runSubelementSelectionCurve(
                profile, 8, targetCount, sharedGeometry, false, curveSamples,
                curve, reason)) {
            results.push_back(curve);
          }
          else {
            unavailable.push_back(
              std::string("subelement_selection_targets:DrawList ") +
              (profile == GLTestProfile::Core ? "core: " :
                                                "compatibility: ") + reason);
          }
          if (sharedGeometry) {
            Measurement churnCurve;
            std::string churnReason;
            if (runSubelementSelectionCurve(
                  profile, 8, targetCount, true, true, curveSamples,
                  churnCurve, churnReason)) {
              results.push_back(churnCurve);
            }
            else {
              unavailable.push_back(
                std::string("subelement_selection_churn:DrawList ") +
                (profile == GLTestProfile::Core ? "core: " :
                                                  "compatibility: ") +
                churnReason);
            }
          }
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
    runSelectionCurves();
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
  if (options.assemblyRebuildOnly > 0) {
    Measurement rebuild;
    std::string reason;
    if (runVariant(GLTestProfile::Core,
                   SoRenderManager::RenderPipeline::DRAW_LIST,
                   "DrawList", WorkloadKind::SharedAssemblyRecipe,
                   options.assemblyRebuildOnly, samples,
                   rebuild, reason, true, options.phaseTiming)) {
      results.push_back(rebuild);
    }
    else unavailable.push_back("shared_assembly_recipe:DrawList core: " +
                               reason);
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
  runSelectionCurves();
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
