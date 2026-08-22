#include "support/GLTestContext.h"
#include "support/RenderWorkloads.h"
#include "rendering/SoGLRenderBackend.h"
#include "rendering/SoRenderPlan.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoPath.h>
#include <Inventor/lists/SoPickedPointList.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/system/gl.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/actions/SoIRRenderAction.h>
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
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTranslation.h>

#include <algorithm>
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

using namespace coin_test;
using Clock = std::chrono::steady_clock;

struct Options {
  bool smoke = false;
  int samples = 0;
  int rebuildOnly = 0;
  int mutationOnly = 0;
  int interactionOnly = 0;
  std::string output;
};

struct Measurement {
  std::string workload;
  std::string renderer;
  std::string profile;
  std::string executionMode;
  int semanticDraws = 0;
  uint64_t submittedDrawCalls = 0;
  uint64_t instancedTriangleBatches = 0;
  uint64_t instancedTriangleCommands = 0;
  uint64_t instancedLineBatches = 0;
  uint64_t instancedLineCommands = 0;
  uint64_t selectionTargets = 0;
  uint64_t selectionDrawCalls = 0;
  uint64_t selectionInstancedBatches = 0;
  uint64_t selectionInstancedCommands = 0;
  uint64_t pickDrawCalls = 0;
  uint64_t pickInstancedBatches = 0;
  uint64_t pickInstancedCommands = 0;
  uint64_t pickDepthDrawCalls = 0;
  uint64_t pickDepthInstancedBatches = 0;
  uint64_t pickDepthInstancedCommands = 0;
  int samples = 0;
  double cpuMedianMs = 0.0;
  double cpuP95Ms = 0.0;
  double gpuMedianMs = 0.0;
  double gpuP95Ms = 0.0;
  double completionMedianMs = 0.0;
  double completionP95Ms = 0.0;
  double mutationMedianMs = 0.0;
  double mutationP95Ms = 0.0;
  double drawListConstructionMedianMs = 0.0;
  double primitiveGenerationMedianMs = 0.0;
  double geometryPackingMedianMs = 0.0;
  double commandEmissionMedianMs = 0.0;
  double planConstructionMedianMs = 0.0;
  double backendSubmissionMedianMs = 0.0;
  double backendFrameSetupMedianMs = 0.0;
  double backendResourcePreparationMedianMs = 0.0;
  double backendCommandExecutionMedianMs = 0.0;
  double backendSelectionMedianMs = 0.0;
  uint64_t drawListRebuilds = 0;
  uint64_t incrementalCommandUpdates = 0;
  double coldPickMs = 0.0;
  double coldPickBufferUpdateMs = 0.0;
  double coldPickTargetPreparationMs = 0.0;
  double coldPickTargetRenderingMs = 0.0;
  double refreshPickMs = 0.0;
  double refreshPickP95Ms = 0.0;
  double refreshPickGpuMedianMs = 0.0;
  double refreshPickGpuP95Ms = 0.0;
  double refreshPickBufferUpdateMs = 0.0;
  double refreshPickTargetPreparationMs = 0.0;
  double refreshPickTargetRenderingMs = 0.0;
  double refreshPickReadbackMedianMs = 0.0;
  double refreshPickReadbackP95Ms = 0.0;
  double refreshPickResultResolutionMedianMs = 0.0;
  double asyncPickRequestMedianMs = 0.0;
  double asyncPickRequestP95Ms = 0.0;
  double asyncPickPollMedianMs = 0.0;
  double asyncPickCompletionMedianMs = 0.0;
  double asyncPickCompletionP95Ms = 0.0;
  double pickMedianMs = 0.0;
  double pickP95Ms = 0.0;
  double pickQueryMedianMs = 0.0;
  double pickResultResolutionMedianMs = 0.0;
  double pickDepthRenderingMedianMs = 0.0;
  double pickDepthPeelingMedianMs = 0.0;
  double pickReadbackMedianMs = 0.0;
  double pickHitProcessingMedianMs = 0.0;
  double pickTargetRestoreMedianMs = 0.0;
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

bool findVisibleCursor(
  const int width, const int height,
  const std::function<bool(int, int, int)> & hitAt,
  SbVec2s & cursor, const bool requireExactPixel)
{
  const int searchRadius = 4;
  for (int y = 2; y < height; y += searchRadius) {
    for (int x = 2; x < width; x += searchRadius) {
      if (!hitAt(x, y, searchRadius)) continue;
      if (!requireExactPixel) {
        cursor = SbVec2s(static_cast<short>(x), static_cast<short>(y));
        return true;
      }
      for (int py = std::max(0, y - searchRadius);
           py <= std::min(height - 1, y + searchRadius); ++py) {
        for (int px = std::max(0, x - searchRadius);
             px <= std::min(width - 1, x + searchRadius); ++px) {
          if (hitAt(px, py, 0)) {
            cursor = SbVec2s(
              static_cast<short>(px), static_cast<short>(py));
            return true;
          }
        }
      }
    }
  }
  return false;
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
                std::string & unavailable,
                bool forceDrawListRebuild = false)
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
  std::vector<double> drawListConstruction;
  std::vector<double> primitiveGeneration;
  std::vector<double> geometryPacking;
  std::vector<double> commandEmission;
  std::vector<double> planConstruction;
  std::vector<double> backendSubmission;
  std::vector<double> backendFrameSetup;
  std::vector<double> backendResourcePreparation;
  std::vector<double> backendCommandExecution;
  std::vector<double> backendSelection;
  GLuint query = 0;
  glGenQueries(1, &query);
  for (int sample = 0; sample < samples; ++sample) {
    context.bindFramebuffer();
    if (forceDrawListRebuild) manager.invalidateDrawList();
    const Clock::time_point totalStart = Clock::now();
    glBeginQuery(GL_TIME_ELAPSED, query);
    const Clock::time_point cpuStart = Clock::now();
    manager.render(TRUE, TRUE);
    cpu.push_back(elapsedMs(cpuStart));
    const SoRenderManager::RenderPhaseStatistics renderPhases =
      manager.getRenderPhaseStatistics();
    drawListConstruction.push_back(
      renderPhases.drawListConstructionNanoseconds / 1000000.0);
    primitiveGeneration.push_back(
      renderPhases.drawListPrimitiveGenerationNanoseconds / 1000000.0);
    geometryPacking.push_back(
      renderPhases.drawListGeometryPackingNanoseconds / 1000000.0);
    commandEmission.push_back(
      renderPhases.drawListCommandEmissionNanoseconds / 1000000.0);
    planConstruction.push_back(
      renderPhases.planConstructionNanoseconds / 1000000.0);
    backendSubmission.push_back(
      renderPhases.backendSubmissionNanoseconds / 1000000.0);
    backendFrameSetup.push_back(
      renderPhases.backendFrameSetupNanoseconds / 1000000.0);
    backendResourcePreparation.push_back(
      renderPhases.backendResourcePreparationNanoseconds / 1000000.0);
    backendCommandExecution.push_back(
      renderPhases.backendCommandExecutionNanoseconds / 1000000.0);
    backendSelection.push_back(
      renderPhases.backendSelectionNanoseconds / 1000000.0);
    result.submittedDrawCalls = renderPhases.submittedDrawCalls;
    if (renderPhases.semanticDrawCommands != 0) {
      result.semanticDraws = static_cast<int>(
        renderPhases.semanticDrawCommands);
    }
    result.instancedTriangleBatches = renderPhases.instancedTriangleBatches;
    result.instancedTriangleCommands = renderPhases.instancedTriangleCommands;
    result.instancedLineBatches = renderPhases.instancedLineBatches;
    result.instancedLineCommands = renderPhases.instancedLineCommands;
    result.drawListRebuilds += renderPhases.drawListRebuilds;
    glEndQuery(GL_TIME_ELAPSED);
    GLuint64 nanoseconds = 0;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
    completion.push_back(elapsedMs(totalStart));
    gpu.push_back(static_cast<double>(nanoseconds) / 1000000.0);
  }
  glDeleteQueries(1, &query);

  std::vector<double> pick;
  double coldPick = 0.0;
  double coldPickBufferUpdate = 0.0;
  double coldPickTargetPreparation = 0.0;
  double coldPickTargetRendering = 0.0;
  double refreshPick = 0.0;
  double refreshPickBufferUpdate = 0.0;
  double refreshPickTargetPreparation = 0.0;
  double refreshPickTargetRendering = 0.0;
  std::vector<double> pickQuery;
  std::vector<double> pickResultResolution;
  std::vector<double> pickDepthRendering;
  std::vector<double> pickDepthPeeling;
  std::vector<double> pickReadback;
  std::vector<double> pickHitProcessing;
  std::vector<double> pickTargetRestore;
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
    if (pipeline == SoRenderManager::RenderPipeline::DRAW_LIST) {
      const SoRenderManager::RenderPhaseStatistics phases =
        manager.getRenderPhaseStatistics();
      coldPickBufferUpdate =
        phases.pickBufferUpdateNanoseconds / 1000000.0;
      coldPickTargetPreparation =
        phases.backendPickTargetPreparationNanoseconds / 1000000.0;
      coldPickTargetRendering =
        phases.backendPickTargetRenderingNanoseconds / 1000000.0;
    }
    for (int sample = 0; sample < samples; ++sample) {
      const Clock::time_point pickStart = Clock::now();
      performPick();
      pick.push_back(elapsedMs(pickStart));
      if (pipeline == SoRenderManager::RenderPipeline::DRAW_LIST) {
        const SoRenderManager::RenderPhaseStatistics phases =
          manager.getRenderPhaseStatistics();
        pickQuery.push_back(phases.pickQueryNanoseconds / 1000000.0);
        pickResultResolution.push_back(
          phases.pickResultResolutionNanoseconds / 1000000.0);
        pickDepthRendering.push_back(
          phases.backendPickDepthRenderingNanoseconds / 1000000.0);
        pickDepthPeeling.push_back(
          phases.backendPickDepthPeelingNanoseconds / 1000000.0);
        pickReadback.push_back(
          phases.backendPickReadbackNanoseconds / 1000000.0);
        pickHitProcessing.push_back(
          phases.backendPickHitProcessingNanoseconds / 1000000.0);
        pickTargetRestore.push_back(
          phases.backendPickTargetRestoreNanoseconds / 1000000.0);
      }
    }
    scene->touch();
    context.bindFramebuffer();
    manager.render(TRUE, TRUE);
    const Clock::time_point refreshPickStart = Clock::now();
    performPick();
    refreshPick = elapsedMs(refreshPickStart);
    if (pipeline == SoRenderManager::RenderPipeline::DRAW_LIST) {
      const SoRenderManager::RenderPhaseStatistics phases =
        manager.getRenderPhaseStatistics();
      refreshPickBufferUpdate =
        phases.pickBufferUpdateNanoseconds / 1000000.0;
      refreshPickTargetPreparation =
        phases.backendPickTargetPreparationNanoseconds / 1000000.0;
      refreshPickTargetRendering =
        phases.backendPickTargetRenderingNanoseconds / 1000000.0;
    }
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
  result.executionMode = forceDrawListRebuild ? "forced_rebuild" :
    (pipeline == SoRenderManager::RenderPipeline::LEGACY_GL ?
      "per_frame_traversal" : "steady_state");
  if (result.semanticDraws == 0) {
    result.semanticDraws = isAssemblyWorkload(workload)
      ? drawCount * 2 : drawCount;
  }
  result.samples = samples;
  result.cpuMedianMs = percentile(cpu, 0.5);
  result.cpuP95Ms = percentile(cpu, 0.95);
  result.gpuMedianMs = percentile(gpu, 0.5);
  result.gpuP95Ms = percentile(gpu, 0.95);
  result.completionMedianMs = percentile(completion, 0.5);
  result.completionP95Ms = percentile(completion, 0.95);
  result.drawListConstructionMedianMs =
    percentile(drawListConstruction, 0.5);
  result.primitiveGenerationMedianMs = percentile(primitiveGeneration, 0.5);
  result.geometryPackingMedianMs = percentile(geometryPacking, 0.5);
  result.commandEmissionMedianMs = percentile(commandEmission, 0.5);
  result.planConstructionMedianMs = percentile(planConstruction, 0.5);
  result.backendSubmissionMedianMs = percentile(backendSubmission, 0.5);
  result.backendFrameSetupMedianMs = percentile(backendFrameSetup, 0.5);
  result.backendResourcePreparationMedianMs =
    percentile(backendResourcePreparation, 0.5);
  result.backendCommandExecutionMedianMs =
    percentile(backendCommandExecution, 0.5);
  result.backendSelectionMedianMs = percentile(backendSelection, 0.5);
  if (!pick.empty()) {
    result.coldPickMs = coldPick;
    result.coldPickBufferUpdateMs = coldPickBufferUpdate;
    result.coldPickTargetPreparationMs = coldPickTargetPreparation;
    result.coldPickTargetRenderingMs = coldPickTargetRendering;
    result.refreshPickMs = refreshPick;
    result.refreshPickBufferUpdateMs = refreshPickBufferUpdate;
    result.refreshPickTargetPreparationMs = refreshPickTargetPreparation;
    result.refreshPickTargetRenderingMs = refreshPickTargetRendering;
    result.pickMedianMs = percentile(pick, 0.5);
    result.pickP95Ms = percentile(pick, 0.95);
    if (!pickQuery.empty()) {
      result.pickQueryMedianMs = percentile(pickQuery, 0.5);
      result.pickResultResolutionMedianMs =
        percentile(pickResultResolution, 0.5);
      result.pickDepthRenderingMedianMs = percentile(pickDepthRendering, 0.5);
      result.pickDepthPeelingMedianMs = percentile(pickDepthPeeling, 0.5);
      result.pickReadbackMedianMs = percentile(pickReadback, 0.5);
      result.pickHitProcessingMedianMs = percentile(pickHitProcessing, 0.5);
      result.pickTargetRestoreMedianMs = percentile(pickTargetRestore, 0.5);
    }
  }
  result.pixelChecksum = pixelChecksum;
  if (forceDrawListRebuild &&
      result.drawListRebuilds != static_cast<uint64_t>(samples)) {
    unavailable = "forced rebuild did not rebuild every measured frame";
    return false;
  }
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
      mutations.coordinates.size() != static_cast<size_t>(drawCount) ||
      mutations.visibilitySwitches.size() !=
        static_cast<size_t>(drawCount) ||
      mutations.structuralBranches.size() !=
        static_cast<size_t>(drawCount)) {
    unavailable = "mutation scene did not expose one target per command";
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

  const auto measure = [&](const std::string & mutation, int changedCount,
                           const std::function<void(int)> & mutate) {
    const bool preservesRenderPlan =
      mutation == "translation" || mutation == "material";
    std::vector<double> frameTimes;
    std::vector<double> constructionTimes;
    std::vector<double> planTimes;
    for (int sample = 0; sample < samples; ++sample) {
      mutate(sample);
      context.bindFramebuffer();
      const Clock::time_point start = Clock::now();
      manager.render(TRUE, TRUE);
      frameTimes.push_back(elapsedMs(start));
      const SoRenderManager::RenderPhaseStatistics phases =
        manager.getRenderPhaseStatistics();
      constructionTimes.push_back(
        phases.drawListConstructionNanoseconds / 1000000.0);
      planTimes.push_back(phases.planConstructionNanoseconds / 1000000.0);
      if (phases.drawListRebuilds != 0 ||
          phases.incrementalCommandUpdates !=
            static_cast<uint64_t>(changedCount) ||
          (preservesRenderPlan
             ? phases.planConstructionNanoseconds != 0
             : phases.planConstructionNanoseconds == 0)) {
        std::ostringstream reason;
        reason << mutation << '_' << changedCount << " updated "
               << phases.incrementalCommandUpdates << " commands with "
               << phases.drawListRebuilds << " rebuilds";
        unavailable = reason.str();
        return false;
      }
    }

    Measurement result;
    result.workload = "incremental_" + mutation + '_' +
      std::to_string(changedCount) + "_of_" + std::to_string(drawCount);
    result.renderer = "DrawList";
    result.profile = profile == GLTestProfile::Core
      ? "core" : "compatibility";
    result.executionMode = "incremental_update";
    result.semanticDraws = drawCount;
    result.samples = samples;
    result.cpuMedianMs = percentile(frameTimes, 0.5);
    result.cpuP95Ms = percentile(frameTimes, 0.95);
    result.completionMedianMs = result.cpuMedianMs;
    result.completionP95Ms = result.cpuP95Ms;
    result.mutationMedianMs = result.cpuMedianMs;
    result.mutationP95Ms = result.cpuP95Ms;
    result.drawListConstructionMedianMs =
      percentile(constructionTimes, 0.5);
    result.planConstructionMedianMs = percentile(planTimes, 0.5);
    result.incrementalCommandUpdates = static_cast<uint64_t>(changedCount);
    result.pixelChecksum = checksumPixels(context.readPixels());
    results.push_back(result);
    return true;
  };

  std::vector<int> changedCounts;
  const int requestedCounts[] = { 1, 10, 100 };
  for (int requested : requestedCounts) {
    const int count = std::min(drawCount, requested);
    if (changedCounts.empty() || changedCounts.back() != count) {
      changedCounts.push_back(count);
    }
  }

  bool valid = true;
  for (int changedCount : changedCounts) {
    std::vector<SbVec3f> translations(static_cast<size_t>(changedCount));
    std::vector<SbVec3f> positions(static_cast<size_t>(changedCount));
    for (int i = 0; i < changedCount; ++i) {
      translations[static_cast<size_t>(i)] =
        mutations.transforms[static_cast<size_t>(i)]->translation.getValue();
      positions[static_cast<size_t>(i)] =
        mutations.coordinates[static_cast<size_t>(i)]->point[0];
    }
    valid = measure("translation", changedCount, [&](int sample) {
      const float offset = (sample & 1) ? -0.01f : 0.01f;
      for (int i = 0; i < changedCount; ++i) {
        mutations.transforms[static_cast<size_t>(i)]->translation =
          translations[static_cast<size_t>(i)] + SbVec3f(offset, 0.0f, 0.0f);
      }
    });
    if (!valid) break;
    valid = measure("material", changedCount, [&](int sample) {
      const SbColor color = (sample & 1)
        ? SbColor(0.75f, 0.25f, 0.15f) : SbColor(0.15f, 0.55f, 0.85f);
      for (int i = 0; i < changedCount; ++i) {
        mutations.materials[static_cast<size_t>(i)]->diffuseColor = color;
      }
    });
    if (!valid) break;
    if (changedCount == 1) {
      valid = measure("geometry", changedCount, [&](int sample) {
        const float offset = (sample & 1) ? -0.01f : 0.01f;
        mutations.coordinates[0]->point.set1Value(
          0, positions[0] + SbVec3f(offset, 0.0f, 0.0f));
      });
      if (!valid) break;
    }
  }

  const int requestedVisibilityCounts[] = { 1, 10, 100, 1000 };
  for (int requested : requestedVisibilityCounts) {
    if (!valid || requested > drawCount) break;
    valid = measure("visibility", requested, [&](int sample) {
      const int child = (sample & 1) ? SO_SWITCH_ALL : SO_SWITCH_NONE;
      for (int i = 0; i < requested; ++i) {
        mutations.visibilitySwitches[static_cast<size_t>(i)]->whichChild =
          child;
      }
    });
    // Keep each curve independent of the final state left by the previous
    // sample count.
    for (int i = 0; i < requested; ++i) {
      mutations.visibilitySwitches[static_cast<size_t>(i)]->whichChild =
        SO_SWITCH_ALL;
    }
    context.bindFramebuffer();
    manager.render(TRUE, TRUE);
  }

  if (valid && drawCount > 256) {
    const int changedCount = 257;
    std::vector<double> frameTimes;
    std::vector<double> constructionTimes;
    for (int sample = 0; sample < samples; ++sample) {
      const SbColor color = (sample & 1)
        ? SbColor(0.65f, 0.3f, 0.2f) : SbColor(0.2f, 0.45f, 0.7f);
      for (int i = 0; i < changedCount; ++i) {
        mutations.materials[static_cast<size_t>(i)]->diffuseColor = color;
      }
      context.bindFramebuffer();
      const Clock::time_point start = Clock::now();
      manager.render(TRUE, TRUE);
      frameTimes.push_back(elapsedMs(start));
      const SoRenderManager::RenderPhaseStatistics phases =
        manager.getRenderPhaseStatistics();
      constructionTimes.push_back(
        phases.drawListConstructionNanoseconds / 1000000.0);
      if (phases.drawListRebuilds != 1 ||
          phases.incrementalCommandUpdates != 0) {
        unavailable = "bounded material batch did not use full rebuild";
        valid = false;
        break;
      }
    }
    if (valid) {
      Measurement result;
      result.workload = "material_fallback_257_of_" +
        std::to_string(drawCount);
      result.renderer = "DrawList";
      result.profile = profile == GLTestProfile::Core
        ? "core" : "compatibility";
      result.executionMode = "full_rebuild_fallback";
      result.semanticDraws = drawCount;
      result.samples = samples;
      result.cpuMedianMs = percentile(frameTimes, 0.5);
      result.cpuP95Ms = percentile(frameTimes, 0.95);
      result.completionMedianMs = result.cpuMedianMs;
      result.completionP95Ms = result.cpuP95Ms;
      result.mutationMedianMs = result.cpuMedianMs;
      result.mutationP95Ms = result.cpuP95Ms;
      result.drawListConstructionMedianMs =
        percentile(constructionTimes, 0.5);
      result.drawListRebuilds = static_cast<uint64_t>(samples);
      result.pixelChecksum = checksumPixels(context.readPixels());
      results.push_back(result);
    }
  }

  if (valid) {
    SoSeparator * branch = mutations.structuralBranches[0];
    SoFaceSet * insertedFace = new SoFaceSet;
    insertedFace->ref();
    insertedFace->numVertices.set1Value(0, 3);
    const std::vector<uint8_t> baselinePixels = context.readPixels();
    std::vector<double> frameTimes;
    std::vector<double> constructionTimes;
    std::vector<double> planTimes;
    for (int sample = 0; sample < samples; ++sample) {
      const int child = branch->findChild(insertedFace);
      if ((sample & 1) == 0 && child < 0) branch->addChild(insertedFace);
      else if ((sample & 1) != 0 && child >= 0) branch->removeChild(child);
      context.bindFramebuffer();
      const Clock::time_point start = Clock::now();
      manager.render(TRUE, TRUE);
      frameTimes.push_back(elapsedMs(start));
      const SoRenderManager::RenderPhaseStatistics phases =
        manager.getRenderPhaseStatistics();
      constructionTimes.push_back(
        phases.drawListConstructionNanoseconds / 1000000.0);
      planTimes.push_back(phases.planConstructionNanoseconds / 1000000.0);
      if (phases.drawListRebuilds != 1 ||
          phases.incrementalCommandUpdates != 0 ||
          phases.drawListConstructionNanoseconds == 0 ||
          phases.planConstructionNanoseconds == 0) {
        unavailable = "structural edit did not rebuild retained state";
        valid = false;
        break;
      }
    }
    const int insertedChild = branch->findChild(insertedFace);
    if (insertedChild >= 0) branch->removeChild(insertedChild);
    context.bindFramebuffer();
    manager.render(TRUE, TRUE);
    if (valid && baselinePixels != context.readPixels()) {
      unavailable = "structural edit did not restore baseline output";
      valid = false;
    }
    if (valid) {
      Measurement result;
      result.workload = "structural_insert_remove_1_of_" +
        std::to_string(drawCount);
      result.renderer = "DrawList";
      result.profile = profile == GLTestProfile::Core
        ? "core" : "compatibility";
      result.executionMode = "full_rebuild";
      result.semanticDraws = drawCount;
      result.samples = samples;
      result.cpuMedianMs = percentile(frameTimes, 0.5);
      result.cpuP95Ms = percentile(frameTimes, 0.95);
      result.completionMedianMs = result.cpuMedianMs;
      result.completionP95Ms = result.cpuP95Ms;
      result.mutationMedianMs = result.cpuMedianMs;
      result.mutationP95Ms = result.cpuP95Ms;
      result.drawListConstructionMedianMs =
        percentile(constructionTimes, 0.5);
      result.planConstructionMedianMs = percentile(planTimes, 0.5);
      result.drawListRebuilds = static_cast<uint64_t>(samples);
      result.pixelChecksum = checksumPixels(baselinePixels);
      results.push_back(result);
    }
    insertedFace->unref();
  }

  manager.releaseRenderBackendResources();
  manager.setCamera(NULL);
  manager.setSceneGraph(NULL);
  camera->unref();
  scene->unref();
  return valid;
}

bool runAssemblyMutations(GLTestProfile profile, WorkloadKind workload,
                          int occurrenceCount, int samples,
                          std::vector<Measurement> & results,
                          std::string & unavailable,
                          bool hoverOnly = false)
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
    workload, occurrenceCount, camera, &mutations);
  const int definitionCount = assemblyDefinitionCount(occurrenceCount);
  if (mutations.coordinates.size() !=
        static_cast<size_t>(occurrenceCount) ||
      mutations.materials.size() != static_cast<size_t>(occurrenceCount) ||
      mutations.definitionCoordinates.size() !=
        static_cast<size_t>(definitionCount)) {
    unavailable = "assembly scene did not expose geometry mutation targets";
    camera->unref();
    scene->unref();
    return false;
  }

  SoCoordinate3 * target = workload == WorkloadKind::SharedAssemblyExpanded
    ? mutations.coordinates[0] : mutations.definitionCoordinates[0];
  const int firstDefinitionOccurrences = workload ==
      WorkloadKind::SharedAssemblyExpanded
    ? 1 : std::min(occurrenceCount,
        (occurrenceCount + definitionCount - 1) / definitionCount);
  // Every assembly occurrence emits one face and one edge command.
  const uint64_t expectedUpdates =
    static_cast<uint64_t>(firstDefinitionOccurrences) * 2;
  const SbVec3f originalPosition = target->point[0];

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

  // Occurrence transforms affect both the face and edge command emitted by
  // each assembly instance. Measure the scaling of the manager's batched
  // matrix replay before changing shared geometry below.
  std::vector<SbVec3f> originalTranslations;
  originalTranslations.reserve(mutations.transforms.size());
  for (SoTranslation * transform : mutations.transforms) {
    originalTranslations.push_back(transform->translation.getValue());
  }
  if (workload == WorkloadKind::SharedAssemblyRecipe) {
    const auto pickAt = [&](const SbVec2s & cursor, SoNode *& identity) {
      SoPickedPoint * picked = NULL;
      if (!manager.pickClosest(cursor[0], cursor[1], 4, picked) || !picked) {
        return false;
      }
      identity = NULL;
      SoPath * path = picked->getPath();
      for (int i = 0; path && i < path->getLength(); ++i) {
        if (path->getNode(i)->isOfType(SoTranslation::getClassTypeId())) {
          identity = path->getNode(i);
        }
      }
      delete picked;
      return true;
    };

    Measurement hover;
    hover.workload = "shared_assembly_hover_pick_" +
      std::to_string(occurrenceCount);
    hover.renderer = "DrawList";
    hover.profile = profile == GLTestProfile::Core
      ? "core" : "compatibility";
    hover.executionMode = "interaction";
    hover.semanticDraws = occurrenceCount * 2;
    hover.samples = samples;
    SoPickedPointList visible;
    Clock::time_point start = Clock::now();
    if (!manager.pickVisibleRegion(SbBox2s(0, 0, 255, 255), visible) ||
        visible.getLength() == 0) {
      unavailable = "assembly cold pick target contained no occurrences";
      manager.releaseRenderBackendResources();
      manager.setCamera(NULL);
      manager.setSceneGraph(NULL);
      camera->unref();
      scene->unref();
      return false;
    }
    std::vector<SbVec2s> pickCursors;
    const size_t desiredCursors = static_cast<size_t>(
      std::max(16, samples * 4));
    for (int y = 2; y < 256 && pickCursors.size() < desiredCursors; y += 4) {
      for (int x = 2; x < 256 && pickCursors.size() < desiredCursors; x += 4) {
        SoNode * candidateIdentity = NULL;
        const SbVec2s candidate(static_cast<short>(x),
                                static_cast<short>(y));
        if (pickAt(candidate, candidateIdentity)) {
          pickCursors.push_back(candidate);
        }
      }
    }
    if (pickCursors.size() < static_cast<size_t>(std::min(samples, 2))) {
      unavailable = "assembly hover setup found too few visible occurrences";
      manager.releaseRenderBackendResources();
      manager.setCamera(NULL);
      manager.setSceneGraph(NULL);
      camera->unref();
      scene->unref();
      return false;
    }

    // Discovery is setup work. Recreate the backend target so cold latency
    // measures the same closest-hit operation used by warm hover queries.
    manager.releaseRenderBackendResources();
    context.bindFramebuffer();
    manager.render(TRUE, TRUE);
    SoNode * identity = NULL;
    start = Clock::now();
    if (!pickAt(pickCursors.front(), identity)) {
      unavailable = "assembly cold hover pick returned no occurrence";
      manager.releaseRenderBackendResources();
      manager.setCamera(NULL);
      manager.setSceneGraph(NULL);
      camera->unref();
      scene->unref();
      return false;
    }
    hover.coldPickMs = elapsedMs(start);
    SoRenderManager::RenderPhaseStatistics pickPhases =
      manager.getRenderPhaseStatistics();
    hover.coldPickBufferUpdateMs =
      pickPhases.pickBufferUpdateNanoseconds / 1000000.0;
    hover.coldPickTargetPreparationMs =
      pickPhases.backendPickTargetPreparationNanoseconds / 1000000.0;
    hover.coldPickTargetRenderingMs =
      pickPhases.backendPickTargetRenderingNanoseconds / 1000000.0;

    std::vector<double> pickTimes;
    std::vector<double> pickQueryTimes;
    std::vector<double> pickResolutionTimes;
    SbVec2s refreshCursor;
    for (int sample = 0; sample < samples; ++sample) {
      const SbVec2s cursor = pickCursors[
        static_cast<size_t>((sample * 7919) % pickCursors.size())];
      start = Clock::now();
      if (!pickAt(cursor, identity)) {
        unavailable = "assembly warm hover pick returned no occurrence";
        manager.releaseRenderBackendResources();
        manager.setCamera(NULL);
        manager.setSceneGraph(NULL);
        camera->unref();
        scene->unref();
        return false;
      }
      pickTimes.push_back(elapsedMs(start));
      if (sample == 0) refreshCursor = cursor;
      pickPhases = manager.getRenderPhaseStatistics();
      pickQueryTimes.push_back(
        pickPhases.pickQueryNanoseconds / 1000000.0);
      pickResolutionTimes.push_back(
        pickPhases.pickResultResolutionNanoseconds / 1000000.0);
      if (pickPhases.pickBufferRefreshes != 0 ||
          pickPhases.drawListRebuilds != 0) {
        unavailable = "warm assembly hover unexpectedly refreshed state";
        manager.releaseRenderBackendResources();
        manager.setCamera(NULL);
        manager.setSceneGraph(NULL);
        camera->unref();
        scene->unref();
        return false;
      }
    }
    hover.pickMedianMs = percentile(pickTimes, 0.5);
    hover.pickP95Ms = percentile(pickTimes, 0.95);
    hover.pickQueryMedianMs = percentile(pickQueryTimes, 0.5);
    hover.pickResultResolutionMedianMs =
      percentile(pickResolutionTimes, 0.5);

    const uint64_t semanticPickCommands =
      static_cast<uint64_t>(occurrenceCount) * 2;
    std::vector<double> refreshTimes;
    std::vector<double> refreshGpuTimes;
    std::vector<double> refreshUpdateTimes;
    std::vector<double> refreshPreparationTimes;
    std::vector<double> refreshRenderingTimes;
    std::vector<double> refreshReadbackTimes;
    std::vector<double> refreshResolutionTimes;
    GLuint refreshQuery = 0;
    glGenQueries(1, &refreshQuery);
    for (int sample = 0; sample < samples; ++sample) {
      const float offset = (sample & 1) ? -0.001f : 0.001f;
      mutations.transforms[0]->translation = originalTranslations[0] +
        SbVec3f(offset, 0.0f, 0.0f);
      context.bindFramebuffer();
      manager.render(TRUE, TRUE);
      glBeginQuery(GL_TIME_ELAPSED, refreshQuery);
      start = Clock::now();
      if (!pickAt(refreshCursor, identity)) {
        unavailable = "assembly hover refresh returned no occurrence";
        glEndQuery(GL_TIME_ELAPSED);
        glDeleteQueries(1, &refreshQuery);
        manager.releaseRenderBackendResources();
        manager.setCamera(NULL);
        manager.setSceneGraph(NULL);
        camera->unref();
        scene->unref();
        return false;
      }
      refreshTimes.push_back(elapsedMs(start));
      glEndQuery(GL_TIME_ELAPSED);
      GLuint64 gpuNanoseconds = 0;
      glGetQueryObjectui64v(
        refreshQuery, GL_QUERY_RESULT, &gpuNanoseconds);
      refreshGpuTimes.push_back(
        static_cast<double>(gpuNanoseconds) / 1000000.0);
      pickPhases = manager.getRenderPhaseStatistics();
      refreshUpdateTimes.push_back(
        pickPhases.pickBufferUpdateNanoseconds / 1000000.0);
      refreshPreparationTimes.push_back(
        pickPhases.backendPickTargetPreparationNanoseconds / 1000000.0);
      refreshRenderingTimes.push_back(
        pickPhases.backendPickTargetRenderingNanoseconds / 1000000.0);
      refreshReadbackTimes.push_back(
        pickPhases.backendPickReadbackNanoseconds / 1000000.0);
      refreshResolutionTimes.push_back(
        pickPhases.pickResultResolutionNanoseconds / 1000000.0);
      hover.pickDrawCalls = pickPhases.pickDrawCalls;
      hover.pickInstancedBatches = pickPhases.pickInstancedBatches;
      hover.pickInstancedCommands = pickPhases.pickInstancedCommands;
      if (pickPhases.pickBufferRefreshes != 1 ||
          hover.pickInstancedBatches == 0 ||
          hover.pickInstancedCommands != semanticPickCommands ||
          hover.pickDrawCalls >= semanticPickCommands) {
        unavailable = "assembly hover refresh invariant failed";
        glDeleteQueries(1, &refreshQuery);
        manager.releaseRenderBackendResources();
        manager.setCamera(NULL);
        manager.setSceneGraph(NULL);
        camera->unref();
        scene->unref();
        return false;
      }
    }
    glDeleteQueries(1, &refreshQuery);
    hover.refreshPickMs = percentile(refreshTimes, 0.5);
    hover.refreshPickP95Ms = percentile(refreshTimes, 0.95);
    hover.refreshPickGpuMedianMs = percentile(refreshGpuTimes, 0.5);
    hover.refreshPickGpuP95Ms = percentile(refreshGpuTimes, 0.95);
    hover.refreshPickBufferUpdateMs = percentile(refreshUpdateTimes, 0.5);
    hover.refreshPickTargetPreparationMs =
      percentile(refreshPreparationTimes, 0.5);
    hover.refreshPickTargetRenderingMs =
      percentile(refreshRenderingTimes, 0.5);
    hover.refreshPickReadbackMedianMs =
      percentile(refreshReadbackTimes, 0.5);
    hover.refreshPickReadbackP95Ms = percentile(refreshReadbackTimes, 0.95);
    hover.refreshPickResultResolutionMedianMs =
      percentile(refreshResolutionTimes, 0.5);

    mutations.transforms[0]->translation = originalTranslations[0];
    context.bindFramebuffer();
    manager.render(TRUE, TRUE);
    hover.pixelChecksum = checksumPixels(context.readPixels());
    results.push_back(hover);
    if (hoverOnly) {
      manager.releaseRenderBackendResources();
      manager.setCamera(NULL);
      manager.setSceneGraph(NULL);
      camera->unref();
      scene->unref();
      return true;
    }
  }
  const int requestedTransformCounts[] = { 1, 10, 100 };
  for (size_t batchIndex = 0;
       batchIndex < sizeof(requestedTransformCounts) /
         sizeof(requestedTransformCounts[0]);
       ++batchIndex) {
    const int changedCount = std::min(
      occurrenceCount, requestedTransformCounts[batchIndex]);
    if (batchIndex > 0 && changedCount == std::min(
          occurrenceCount, requestedTransformCounts[batchIndex - 1])) {
      continue;
    }
    std::vector<double> frameTimes;
    std::vector<double> constructionTimes;
    std::vector<double> planTimes;
    const float magnitude = 0.002f * static_cast<float>(batchIndex + 1);
    for (int sample = 0; sample < samples; ++sample) {
      const float offset = (sample & 1) ? -magnitude : magnitude;
      for (int occurrence = 0; occurrence < changedCount; ++occurrence) {
        mutations.transforms[static_cast<size_t>(occurrence)]->translation =
          originalTranslations[static_cast<size_t>(occurrence)] +
          SbVec3f(offset, 0.0f, 0.0f);
      }
      context.bindFramebuffer();
      const Clock::time_point start = Clock::now();
      manager.render(TRUE, TRUE);
      frameTimes.push_back(elapsedMs(start));
      const SoRenderManager::RenderPhaseStatistics phases =
        manager.getRenderPhaseStatistics();
      constructionTimes.push_back(
        phases.drawListConstructionNanoseconds / 1000000.0);
      planTimes.push_back(phases.planConstructionNanoseconds / 1000000.0);
      const uint64_t expectedUpdates =
        static_cast<uint64_t>(changedCount) * 2;
      if (phases.drawListRebuilds != 0 ||
          phases.incrementalCommandUpdates != expectedUpdates) {
        std::ostringstream reason;
        reason << "assembly transform batch " << changedCount << " updated "
               << phases.incrementalCommandUpdates << " commands with "
               << phases.drawListRebuilds << " rebuilds; expected "
               << expectedUpdates;
        unavailable = reason.str();
        manager.releaseRenderBackendResources();
        manager.setCamera(NULL);
        manager.setSceneGraph(NULL);
        camera->unref();
        scene->unref();
        return false;
      }
    }

    const std::vector<uint8_t> incrementalPixels = context.readPixels();
    manager.invalidateDrawList();
    context.bindFramebuffer();
    manager.render(TRUE, TRUE);
    if (incrementalPixels != context.readPixels()) {
      unavailable = "assembly transform batch differs from a forced rebuild";
      manager.releaseRenderBackendResources();
      manager.setCamera(NULL);
      manager.setSceneGraph(NULL);
      camera->unref();
      scene->unref();
      return false;
    }

    Measurement transformResult;
    transformResult.workload = "incremental_" +
      std::string(workloadName(workload)) + "_translation_" +
      std::to_string(changedCount) + "_of_" +
      std::to_string(occurrenceCount);
    transformResult.renderer = "DrawList";
    transformResult.profile = profile == GLTestProfile::Core
      ? "core" : "compatibility";
    transformResult.executionMode = "incremental_update";
    transformResult.semanticDraws = occurrenceCount * 2;
    transformResult.samples = samples;
    transformResult.cpuMedianMs = percentile(frameTimes, 0.5);
    transformResult.cpuP95Ms = percentile(frameTimes, 0.95);
    transformResult.completionMedianMs = transformResult.cpuMedianMs;
    transformResult.completionP95Ms = transformResult.cpuP95Ms;
    transformResult.mutationMedianMs = transformResult.cpuMedianMs;
    transformResult.mutationP95Ms = transformResult.cpuP95Ms;
    transformResult.drawListConstructionMedianMs =
      percentile(constructionTimes, 0.5);
    transformResult.planConstructionMedianMs = percentile(planTimes, 0.5);
    transformResult.incrementalCommandUpdates =
      static_cast<uint64_t>(changedCount) * 2;
    transformResult.pixelChecksum = checksumPixels(incrementalPixels);
    results.push_back(transformResult);
  }

  std::vector<SbColor> originalMaterialColors;
  originalMaterialColors.reserve(mutations.materials.size());
  for (SoMaterial * material : mutations.materials) {
    originalMaterialColors.push_back(material->diffuseColor[0]);
  }
  const auto measureOccurrenceMaterial =
    [&](const std::string & label, const int changedCount,
        const std::function<void(int)> & mutate) {
      std::vector<double> materialFrameTimes;
      std::vector<double> materialPlanTimes;
      for (int sample = 0; sample < samples; ++sample) {
        mutate(sample);
        context.bindFramebuffer();
        const Clock::time_point start = Clock::now();
        manager.render(TRUE, TRUE);
        materialFrameTimes.push_back(elapsedMs(start));
        const SoRenderManager::RenderPhaseStatistics phases =
          manager.getRenderPhaseStatistics();
        materialPlanTimes.push_back(
          phases.planConstructionNanoseconds / 1000000.0);
        if (phases.drawListRebuilds != 0 ||
            phases.incrementalCommandUpdates !=
              static_cast<uint64_t>(changedCount) ||
            phases.planConstructionNanoseconds == 0) {
          std::ostringstream reason;
          reason << label << " updated " << phases.incrementalCommandUpdates
                 << " commands with " << phases.drawListRebuilds
                 << " rebuilds and " << phases.planConstructionNanoseconds
                 << " ns of plan construction; expected " << changedCount
                 << " incremental updates";
          unavailable = reason.str();
          return false;
        }
      }

      const std::vector<uint8_t> materialPixels = context.readPixels();
      manager.invalidateDrawList();
      context.bindFramebuffer();
      manager.render(TRUE, TRUE);
      if (materialPixels != context.readPixels()) {
        unavailable = label + " differs from a forced rebuild";
        return false;
      }

      Measurement materialResult;
      materialResult.workload = "incremental_" +
        std::string(workloadName(workload)) + '_' + label + '_' +
        std::to_string(changedCount) + "_of_" +
        std::to_string(occurrenceCount);
      materialResult.renderer = "DrawList";
      materialResult.profile = profile == GLTestProfile::Core
        ? "core" : "compatibility";
      materialResult.executionMode = "incremental_update";
      materialResult.semanticDraws = occurrenceCount * 2;
      materialResult.samples = samples;
      materialResult.cpuMedianMs = percentile(materialFrameTimes, 0.5);
      materialResult.cpuP95Ms = percentile(materialFrameTimes, 0.95);
      materialResult.completionMedianMs = materialResult.cpuMedianMs;
      materialResult.completionP95Ms = materialResult.cpuP95Ms;
      materialResult.mutationMedianMs = materialResult.cpuMedianMs;
      materialResult.mutationP95Ms = materialResult.cpuP95Ms;
      materialResult.planConstructionMedianMs =
        percentile(materialPlanTimes, 0.5);
      materialResult.incrementalCommandUpdates = changedCount;
      materialResult.pixelChecksum = checksumPixels(materialPixels);
      results.push_back(materialResult);
      return true;
    };

  const int requestedMaterialCounts[] = { 1, 10, 100 };
  int previousMaterialCount = 0;
  for (int requested : requestedMaterialCounts) {
    const int changedCount = std::min(requested, occurrenceCount);
    if (changedCount == previousMaterialCount) continue;
    previousMaterialCount = changedCount;
    if (!measureOccurrenceMaterial(
          "material", changedCount, [&](int sample) {
            const SbColor color = (sample & 1)
              ? SbColor(0.80f, 0.25f, 0.15f)
              : SbColor(0.15f, 0.55f, 0.85f);
            for (int occurrence = 0; occurrence < changedCount;
                 ++occurrence) {
              mutations.materials[static_cast<size_t>(occurrence)]
                ->diffuseColor.set1Value(0, color);
            }
          })) {
      manager.releaseRenderBackendResources();
      manager.setCamera(NULL);
      manager.setSceneGraph(NULL);
      camera->unref();
      scene->unref();
      return false;
    }
  }

  for (size_t occurrence = 0; occurrence < mutations.materials.size();
       ++occurrence) {
    mutations.materials[occurrence]->diffuseColor.set1Value(
      0, originalMaterialColors[occurrence]);
  }
  context.bindFramebuffer();
  manager.render(TRUE, TRUE);
  if (!measureOccurrenceMaterial("opacity_transition", 1, [&](int sample) {
        mutations.materials[0]->transparency.set1Value(
          0, (sample & 1) ? 0.0f : 0.35f);
      })) {
    manager.releaseRenderBackendResources();
    manager.setCamera(NULL);
    manager.setSceneGraph(NULL);
    camera->unref();
    scene->unref();
    return false;
  }
  mutations.materials[0]->transparency.set1Value(0, 0.0f);
  context.bindFramebuffer();
  manager.render(TRUE, TRUE);

  std::vector<double> frameTimes;
  for (int sample = 0; sample < samples; ++sample) {
    const float offset = (sample & 1) ? -0.01f : 0.01f;
    target->point.set1Value(
      0, originalPosition + SbVec3f(offset, 0.0f, 0.0f));
    context.bindFramebuffer();
    const Clock::time_point start = Clock::now();
    manager.render(TRUE, TRUE);
    frameTimes.push_back(elapsedMs(start));
    const SoRenderManager::RenderPhaseStatistics phases =
      manager.getRenderPhaseStatistics();
    if (phases.drawListRebuilds != 0 ||
        phases.incrementalCommandUpdates != expectedUpdates) {
      std::ostringstream reason;
      reason << workloadName(workload) << " updated "
             << phases.incrementalCommandUpdates << " commands with "
             << phases.drawListRebuilds << " rebuilds; expected "
             << expectedUpdates;
      unavailable = reason.str();
      manager.releaseRenderBackendResources();
      manager.setCamera(NULL);
      manager.setSceneGraph(NULL);
      camera->unref();
      scene->unref();
      return false;
    }
  }

  const std::vector<uint8_t> incrementalPixels = context.readPixels();
  manager.invalidateDrawList();
  context.bindFramebuffer();
  manager.render(TRUE, TRUE);
  const std::vector<uint8_t> rebuiltPixels = context.readPixels();
  if (incrementalPixels != rebuiltPixels) {
    unavailable = std::string(workloadName(workload)) +
      " incremental geometry differs from a forced rebuild";
    manager.releaseRenderBackendResources();
    manager.setCamera(NULL);
    manager.setSceneGraph(NULL);
    camera->unref();
    scene->unref();
    return false;
  }

  Measurement result;
  result.workload = std::string("incremental_") + workloadName(workload) +
    "_geometry_definition_" + std::to_string(firstDefinitionOccurrences) +
    "_of_" + std::to_string(occurrenceCount);
  result.renderer = "DrawList";
  result.profile = profile == GLTestProfile::Core
    ? "core" : "compatibility";
  result.executionMode = "incremental_update";
  result.semanticDraws = occurrenceCount * 2;
  result.samples = samples;
  result.cpuMedianMs = percentile(frameTimes, 0.5);
  result.cpuP95Ms = percentile(frameTimes, 0.95);
  result.completionMedianMs = result.cpuMedianMs;
  result.completionP95Ms = result.cpuP95Ms;
  result.mutationMedianMs = result.cpuMedianMs;
  result.mutationP95Ms = result.cpuP95Ms;
  result.incrementalCommandUpdates = expectedUpdates;
  result.pixelChecksum = checksumPixels(rebuiltPixels);
  results.push_back(result);

  std::vector<double> rebuildTimes;
  std::vector<double> rebuildConstructionTimes;
  for (int sample = 0; sample < samples; ++sample) {
    manager.invalidateDrawList();
    context.bindFramebuffer();
    const Clock::time_point start = Clock::now();
    manager.render(TRUE, TRUE);
    rebuildTimes.push_back(elapsedMs(start));
    const SoRenderManager::RenderPhaseStatistics phases =
      manager.getRenderPhaseStatistics();
    rebuildConstructionTimes.push_back(
      phases.drawListConstructionNanoseconds / 1000000.0);
    if (phases.drawListRebuilds != 1 ||
        phases.incrementalCommandUpdates != 0) {
      unavailable = std::string(workloadName(workload)) +
        " forced rebuild did not reconstruct the retained frame";
      manager.releaseRenderBackendResources();
      manager.setCamera(NULL);
      manager.setSceneGraph(NULL);
      camera->unref();
      scene->unref();
      return false;
    }
  }
  Measurement rebuildResult;
  rebuildResult.workload = "forced_" + std::string(workloadName(workload)) +
    "_rebuild_" + std::to_string(occurrenceCount);
  rebuildResult.renderer = "DrawList";
  rebuildResult.profile = profile == GLTestProfile::Core
    ? "core" : "compatibility";
  rebuildResult.executionMode = "forced_rebuild";
  rebuildResult.semanticDraws = occurrenceCount * 2;
  rebuildResult.samples = samples;
  rebuildResult.cpuMedianMs = percentile(rebuildTimes, 0.5);
  rebuildResult.cpuP95Ms = percentile(rebuildTimes, 0.95);
  rebuildResult.completionMedianMs = rebuildResult.cpuMedianMs;
  rebuildResult.completionP95Ms = rebuildResult.cpuP95Ms;
  rebuildResult.drawListConstructionMedianMs =
    percentile(rebuildConstructionTimes, 0.5);
  rebuildResult.drawListRebuilds = static_cast<uint64_t>(samples);
  rebuildResult.pixelChecksum = checksumPixels(context.readPixels());
  results.push_back(rebuildResult);

  manager.releaseRenderBackendResources();
  manager.setCamera(NULL);
  manager.setSceneGraph(NULL);
  camera->unref();
  scene->unref();
  return true;
}

bool runAssemblySelectionInteractions(
  GLTestProfile profile, int occurrenceCount, int samples,
  std::vector<Measurement> & results, std::string & unavailable)
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

  SoOrthographicCamera * camera = NULL;
  SoSeparator * scene = makeScene(
    WorkloadKind::SharedAssemblyRecipe, occurrenceCount, camera);
  SbViewportRegion viewport(SbVec2s(256, 256));
  viewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(256, 256));
  SoIRRenderAction action(viewport);
  action.setCamera(camera);
  action.setCameraPolicy(
    SoIRRenderAction::CameraPolicy::USE_CONFIGURED_CAMERA);
  action.apply(scene);
  SoDrawList & drawlist = action.getMutableDrawList();
  if (drawlist.getNumCommands() != occurrenceCount * 2) {
    unavailable = "assembly selection scene emitted an unexpected command count";
    camera->unref();
    scene->unref();
    return false;
  }

  SoRenderParams params = {};
  params.viewport = viewport;
  SbViewportRegion cameraViewport = viewport;
  static_cast<SoCamera *>(camera)->getViewVolume(
    viewport, cameraViewport).getMatrices(
    params.viewMatrix, params.projMatrix);
  params.viewport = cameraViewport;
  params.clearColor = SbColor4f(0.0f, 0.0f, 0.0f, 1.0f);
  params.clearDepth = 1.0f;
  params.flags = SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH;
  SoRenderPlanner planner;
  SoRenderPlan plan;
  planner.build(drawlist, plan);
  SoGLRenderBackend backend;
  backend.setPhaseTimingEnabled(TRUE);
  SoRenderBackendInitParams initparams = {};
  if (!backend.initialize(initparams) ||
      !backend.render(drawlist, plan, params)) {
    unavailable = "assembly selection backend could not render the scene";
    camera->unref();
    scene->unref();
    return false;
  }
  const uint64_t baselineChecksum = checksumPixels(context.readPixels());

  if (!backend.updatePickBuffer(drawlist, plan, params)) {
    unavailable = "assembly asynchronous pick target update failed";
    backend.shutdown();
    camera->unref();
    scene->unref();
    return false;
  }
  SbVec2s asyncCursor;
  SoPickResult setupHit;
  if (!findVisibleCursor(
        256, 256,
        [&](const int x, const int y, const int radius) {
          return backend.pickClosest(x, y, radius, setupHit) != FALSE;
        },
        asyncCursor, false)) {
    unavailable = "assembly asynchronous pick setup found no visible hit";
    backend.shutdown();
    camera->unref();
    scene->unref();
    return false;
  }
  std::vector<double> asyncRequestTimes;
  std::vector<double> asyncPollTimes;
  std::vector<double> asyncCompletionTimes;
  for (int sample = 0; sample < samples; ++sample) {
    SbMatrix matrix = static_cast<const SoDrawList &>(drawlist)
      .getCommand(0).modelMatrix;
    matrix[3][0] += (sample & 1) ? -0.001f : 0.001f;
    drawlist.getCommandForRetainedUpdate(0).modelMatrix = matrix;
    drawlist.applyRetainedInvalidation(false, false, false);
    if (!backend.updatePickBuffer(drawlist, plan, params)) {
      unavailable = "assembly asynchronous pick refresh failed";
      backend.shutdown();
      camera->unref();
      scene->unref();
      return false;
    }
    SoAsyncPickRequest request;
    const Clock::time_point completionStart = Clock::now();
    Clock::time_point phaseStart = Clock::now();
    if (!backend.requestPickClosestAsync(
          asyncCursor[0], asyncCursor[1], 4, request)) {
      unavailable = "assembly asynchronous pick request failed";
      backend.shutdown();
      camera->unref();
      scene->unref();
      return false;
    }
    asyncRequestTimes.push_back(elapsedMs(phaseStart));
    SoPickResult asyncHit;
    phaseStart = Clock::now();
    SoAsyncPickStatus status =
      backend.pollPickClosestAsync(request, asyncHit);
    asyncPollTimes.push_back(elapsedMs(phaseStart));
    if (status == SoAsyncPickStatus::PENDING) {
      glFinish();
      status = backend.pollPickClosestAsync(request, asyncHit);
    }
    asyncCompletionTimes.push_back(elapsedMs(completionStart));
    if (status != SoAsyncPickStatus::HIT ||
        asyncHit.commandIndex < 0 || asyncHit.id == 0) {
      unavailable = "assembly asynchronous pick did not resolve a hit";
      backend.shutdown();
      camera->unref();
      scene->unref();
      return false;
    }
  }
  Measurement asyncHover;
  asyncHover.workload = "shared_assembly_backend_async_hover_" +
    std::to_string(occurrenceCount);
  asyncHover.renderer = "DrawList";
  asyncHover.profile = profile == GLTestProfile::Core
    ? "core" : "compatibility";
  asyncHover.executionMode = "interaction";
  asyncHover.semanticDraws = occurrenceCount * 2;
  asyncHover.samples = samples;
  asyncHover.asyncPickRequestMedianMs =
    percentile(asyncRequestTimes, 0.5);
  asyncHover.asyncPickRequestP95Ms = percentile(asyncRequestTimes, 0.95);
  asyncHover.asyncPickPollMedianMs = percentile(asyncPollTimes, 0.5);
  asyncHover.asyncPickCompletionMedianMs =
    percentile(asyncCompletionTimes, 0.5);
  asyncHover.asyncPickCompletionP95Ms =
    percentile(asyncCompletionTimes, 0.95);
  asyncHover.pixelChecksum = baselineChecksum;
  results.push_back(asyncHover);

  const auto measureSelection = [&](const char * label,
                                    const int selectedOccurrences,
                                    const bool churn,
                                    const bool subelement,
                                    const bool highlighted) {
    std::vector<double> timings;
    SoRenderBackendStatistics statistics;
    for (int sample = 0; sample < samples; ++sample) {
      context.bindFramebuffer();
      if (!backend.render(drawlist, plan, params)) return false;
      SoSelectionState selection;
      if (highlighted) {
        SoSelectionTarget target;
        target.commandIndex = setupHit.commandIndex;
        target.color = SbColor4f(0.0f, 1.0f, 1.0f, 0.8f);
        selection.highlighted.push_back(target);
      }
      else {
        selection.selected.reserve(
          static_cast<size_t>(selectedOccurrences) * 2);
        const int offset = churn ? sample : 0;
        for (int selected = 0; selected < selectedOccurrences; ++selected) {
          const int occurrence = (selected * 7919 + offset) % occurrenceCount;
          for (int commandOffset = 0; commandOffset < 2; ++commandOffset) {
            SoSelectionTarget target;
            target.commandIndex = occurrence * 2 + commandOffset;
            if (subelement) {
              target.type = commandOffset == 0 ? SO_PICK_FACE : SO_PICK_EDGE;
              // Every assembly definition contains at least eight face and
              // edge ranges, so this remains valid across the shared recipes.
              target.elementIndex = selected % 8;
            }
            target.color = SbColor4f(1.0f, 0.75f, 0.0f, 0.65f);
            selection.selected.push_back(target);
          }
        }
      }
      const Clock::time_point start = Clock::now();
      if (!backend.renderSelection(drawlist, selection, params)) return false;
      timings.push_back(elapsedMs(start));
      statistics = backend.getStatistics();
      const uint64_t expectedTargets = highlighted ? 1 :
        static_cast<uint64_t>(selectedOccurrences) * 2;
      if (statistics.selection.targets != expectedTargets ||
          statistics.selection.drawCalls > expectedTargets ||
          statistics.selection.instancedCommands > expectedTargets ||
          (subelement && selectedOccurrences >= 10 &&
           statistics.selection.instancedCommands == 0)) {
        return false;
      }
    }

    Measurement result;
    result.workload = std::string(label) + '_' +
      std::to_string(occurrenceCount);
    result.renderer = "DrawList";
    result.profile = profile == GLTestProfile::Core
      ? "core" : "compatibility";
    result.executionMode = "selection_interaction";
    result.semanticDraws = occurrenceCount * 2;
    result.samples = samples;
    result.cpuMedianMs = percentile(timings, 0.5);
    result.cpuP95Ms = percentile(timings, 0.95);
    result.backendSelectionMedianMs = result.cpuMedianMs;
    result.selectionTargets = statistics.selection.targets;
    result.selectionDrawCalls = statistics.selection.drawCalls;
    result.selectionInstancedBatches =
      statistics.selection.instancedBatches;
    result.selectionInstancedCommands =
      statistics.selection.instancedCommands;
    result.pixelChecksum = checksumPixels(context.readPixels());
    if (result.pixelChecksum == 0 || result.pixelChecksum == baselineChecksum ||
        (selectedOccurrences >= 10 &&
         result.selectionDrawCalls >= result.selectionTargets)) {
      return false;
    }
    results.push_back(result);
    return true;
  };

  const int onePercent = std::max(1, occurrenceCount / 100);
  const int tenPercent = std::max(1, occurrenceCount / 10);
  const bool valid =
    measureSelection(
      "shared_assembly_selection_1_percent", onePercent,
      false, false, false) &&
    measureSelection(
      "shared_assembly_selection_10_percent", tenPercent,
      false, false, false) &&
    measureSelection(
      "shared_assembly_selection_churn", tenPercent,
      true, false, false) &&
    measureSelection(
      "shared_assembly_subelement_selection", tenPercent,
      true, true, false) &&
    measureSelection(
      "shared_assembly_preselection", 1, false, false, true);
  if (!valid) unavailable = "assembly selection interaction invariant failed";
  backend.shutdown();
  camera->unref();
  scene->unref();
  return valid;
}

bool runAssemblyDepthStack(GLTestProfile profile, int occurrenceCount,
                           int samples, std::vector<Measurement> & results,
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
    WorkloadKind::SharedAssemblyRecipe, occurrenceCount, camera, &mutations);
  if (mutations.transforms.size() != static_cast<size_t>(occurrenceCount)) {
    unavailable = "depth-stack scene did not expose every occurrence";
    camera->unref();
    scene->unref();
    return false;
  }
  SbVec3f stackOrigin =
    mutations.transforms.front()->translation.getValue();
  float nearestCenter = stackOrigin[0] * stackOrigin[0] +
    stackOrigin[1] * stackOrigin[1];
  for (SoTranslation * transform : mutations.transforms) {
    const SbVec3f candidate = transform->translation.getValue();
    const float distance = candidate[0] * candidate[0] +
      candidate[1] * candidate[1];
    if (distance < nearestCenter) {
      stackOrigin = candidate;
      nearestCenter = distance;
    }
  }
  for (int occurrence = 0; occurrence < occurrenceCount; ++occurrence) {
    mutations.transforms[static_cast<size_t>(occurrence)]->translation =
      SbVec3f(stackOrigin[0], stackOrigin[1],
              stackOrigin[2] - 0.05f * static_cast<float>(occurrence));
  }
  camera->farDistance = std::max(
    100.0f, 20.0f + 0.05f * static_cast<float>(occurrenceCount));

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

  // Build the target outside every depth curve so samples measure peeling,
  // readback, and result resolution rather than cold target construction.
  SbVec2s stackCursor;
  if (!findVisibleCursor(
        256, 256,
        [&](const int x, const int y, const int radius) {
          SoPickedPoint * hit = NULL;
          const bool found = manager.pickClosest(x, y, radius, hit) && hit;
          delete hit;
          return found;
        },
        stackCursor, true)) {
    unavailable = "depth-stack scene produced no visible hit";
    manager.releaseRenderBackendResources();
    manager.setCamera(NULL);
    manager.setSceneGraph(NULL);
    camera->unref();
    scene->unref();
    return false;
  }

  bool valid = true;
  const int requestedLayers[] = { 1, 8, 32, 128 };
  int previousLayers = 0;
  for (int requested : requestedLayers) {
    const int maxLayers = std::min(requested, occurrenceCount);
    if (maxLayers == previousLayers) continue;
    previousLayers = maxLayers;
    const int curveSamples = std::min(
      samples, std::max(5, 128 / std::max(1, maxLayers)));
    std::vector<double> totalTimes;
    std::vector<double> queryTimes;
    std::vector<double> resolutionTimes;
    std::vector<double> renderingTimes;
    std::vector<double> peelingTimes;
    std::vector<double> readbackTimes;
    std::vector<double> processingTimes;
    std::vector<double> restoreTimes;
    SoRenderManager::RenderPhaseStatistics phases;
    for (int sample = 0; sample < curveSamples; ++sample) {
      SoPickedPointList stack;
      const Clock::time_point start = Clock::now();
      if (!manager.pickDepthStack(stackCursor[0], stackCursor[1], 0,
                                  maxLayers, stack,
                                  maxLayers)) {
        unavailable = "assembly depth-stack query returned no hits";
        valid = false;
        break;
      }
      totalTimes.push_back(elapsedMs(start));
      phases = manager.getRenderPhaseStatistics();
      queryTimes.push_back(phases.pickQueryNanoseconds / 1000000.0);
      resolutionTimes.push_back(
        phases.pickResultResolutionNanoseconds / 1000000.0);
      renderingTimes.push_back(
        phases.backendPickDepthRenderingNanoseconds / 1000000.0);
      peelingTimes.push_back(
        phases.backendPickDepthPeelingNanoseconds / 1000000.0);
      readbackTimes.push_back(
        phases.backendPickReadbackNanoseconds / 1000000.0);
      processingTimes.push_back(
        phases.backendPickHitProcessingNanoseconds / 1000000.0);
      restoreTimes.push_back(
        phases.backendPickTargetRestoreNanoseconds / 1000000.0);

      std::vector<const SoSeparator *> identities;
      float previousZ = 0.0f;
      bool havePreviousZ = false;
      for (int hitIndex = 0; hitIndex < stack.getLength(); ++hitIndex) {
        SoPickedPoint * hit = stack[hitIndex];
        const float z = hit->getPoint()[2];
        if (havePreviousZ && z > previousZ + 0.0001f) {
          unavailable = "assembly depth-stack hits were not front-to-back";
          valid = false;
          break;
        }
        previousZ = z;
        havePreviousZ = true;
        const SoPath * path = hit->getPath();
        const SoSeparator * identity = NULL;
        for (int node = 0; path && node < path->getLength(); ++node) {
          SoSeparator * candidate = SoSeparator::getClassTypeId() ==
              path->getNode(node)->getTypeId()
            ? static_cast<SoSeparator *>(path->getNode(node)) : NULL;
          if (candidate && std::find(mutations.structuralBranches.begin(),
                                     mutations.structuralBranches.end(),
                                     candidate) !=
                           mutations.structuralBranches.end()) {
            identity = candidate;
            break;
          }
        }
        if (identity && std::find(identities.begin(), identities.end(),
                                  identity) == identities.end()) {
          identities.push_back(identity);
        }
      }
      if (!valid) break;
      const size_t expectedIdentities = static_cast<size_t>(
        std::min(maxLayers, std::min(2, occurrenceCount)));
      if (identities.size() < expectedIdentities ||
          phases.drawListRebuilds != 0 ||
          phases.pickBufferRefreshes != 0 ||
          phases.pickInstancedBatches == 0 ||
          phases.pickInstancedCommands == 0 ||
          phases.pickDrawCalls >=
            static_cast<uint64_t>(occurrenceCount * 2) *
              static_cast<uint64_t>(maxLayers + 1)) {
        std::ostringstream reason;
        reason << "assembly depth-stack batching invariant failed at "
               << maxLayers << " layers (identities=" << identities.size()
               << ", rebuilds=" << phases.drawListRebuilds
               << ", refreshes=" << phases.pickBufferRefreshes
               << ", draws=" << phases.pickDrawCalls
               << ", batches=" << phases.pickInstancedBatches
               << ", commands=" << phases.pickInstancedCommands << ')';
        unavailable = reason.str();
        valid = false;
        break;
      }
    }
    if (!valid) break;

    Measurement result;
    result.workload = "shared_assembly_depth_stack_" +
      std::to_string(maxLayers) + "_of_" +
      std::to_string(occurrenceCount);
    result.renderer = "DrawList";
    result.profile = profile == GLTestProfile::Core
      ? "core" : "compatibility";
    result.executionMode = "depth_stack_interaction";
    result.semanticDraws = occurrenceCount * 2;
    result.samples = curveSamples;
    result.cpuMedianMs = percentile(totalTimes, 0.5);
    result.cpuP95Ms = percentile(totalTimes, 0.95);
    result.pickMedianMs = result.cpuMedianMs;
    result.pickP95Ms = result.cpuP95Ms;
    result.pickQueryMedianMs = percentile(queryTimes, 0.5);
    result.pickResultResolutionMedianMs = percentile(resolutionTimes, 0.5);
    result.pickDepthRenderingMedianMs = percentile(renderingTimes, 0.5);
    result.pickDepthPeelingMedianMs = percentile(peelingTimes, 0.5);
    result.pickReadbackMedianMs = percentile(readbackTimes, 0.5);
    result.pickHitProcessingMedianMs = percentile(processingTimes, 0.5);
    result.pickTargetRestoreMedianMs = percentile(restoreTimes, 0.5);
    result.pickDepthDrawCalls = phases.pickDrawCalls;
    result.pickDepthInstancedBatches = phases.pickInstancedBatches;
    result.pickDepthInstancedCommands = phases.pickInstancedCommands;
    result.pixelChecksum = checksumPixels(context.readPixels());
    results.push_back(result);
  }

  manager.releaseRenderBackendResources();
  manager.setCamera(NULL);
  manager.setSceneGraph(NULL);
  camera->unref();
  scene->unref();
  return valid;
}

void runMutationBenchmarks(GLTestProfile profile, int objectCount, int samples,
                           std::vector<Measurement> & results,
                           std::vector<std::string> & unavailable)
{
  const std::string profileName = profile == GLTestProfile::Core
    ? "core" : "compatibility";
  std::string reason;
  if (!runIncrementalMutationScaling(
        profile, objectCount, samples, results, reason)) {
    unavailable.push_back(
      "incremental mutations DrawList " + profileName + ": " + reason);
  }

  const WorkloadKind assemblies[] = {
    WorkloadKind::SharedAssemblyExpanded,
    WorkloadKind::SharedAssemblySources,
    WorkloadKind::SharedAssemblyRecipe
  };
  for (WorkloadKind workload : assemblies) {
    reason.clear();
    if (!runAssemblyMutations(
          profile, workload, objectCount, samples, results, reason)) {
      unavailable.push_back(std::string(workloadName(workload)) +
        " mutation DrawList " + profileName + ": " + reason);
    }
  }
  reason.clear();
  if (!runAssemblySelectionInteractions(
        profile, objectCount, samples, results, reason)) {
    unavailable.push_back(
      "shared assembly selection DrawList " + profileName + ": " + reason);
  }
  reason.clear();
  if (!runAssemblyDepthStack(
        profile, objectCount, samples, results, reason)) {
    unavailable.push_back(
      "shared assembly depth stack DrawList " + profileName + ": " + reason);
  }
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
    else if (arg == "--mutation-only" && i + 1 < argc)
      options.mutationOnly = std::atoi(argv[++i]);
    else if (arg == "--interaction-only" && i + 1 < argc)
      options.interactionOnly = std::atoi(argv[++i]);
    else if (arg == "--output" && i + 1 < argc) options.output = argv[++i];
    else {
      std::cerr << "Usage: CoinRenderGLBenchmarks [--smoke] [--samples N] "
                   "[--rebuild-only N] [--mutation-only N] "
                   "[--interaction-only N] [--output FILE]\n";
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
  const char * mode = options.interactionOnly > 0 ? "interaction" :
    (options.mutationOnly > 0 ? "mutation" :
      (options.smoke ? "smoke" : "benchmark"));
  out << "{\n  \"schema_version\": 14,\n  \"mode\": \""
      << mode
      << "\",\n  \"time_unit\": \"ms\",\n  \"benchmarks\": [\n";
  for (size_t i = 0; i < results.size(); ++i) {
    const Measurement & r = results[i];
    out << "    {\"workload\": \"" << r.workload
        << "\", \"renderer\": \"" << r.renderer
        << "\", \"profile\": \"" << r.profile
        << "\", \"execution_mode\": \"" << r.executionMode
        << "\", \"semantic_draws\": " << r.semanticDraws
        << ", \"samples\": " << r.samples
        << ", \"cpu_render_median_ms\": " << r.cpuMedianMs
        << ", \"cpu_render_p95_ms\": " << r.cpuP95Ms
        << ", \"gpu_median_ms\": " << r.gpuMedianMs
        << ", \"gpu_p95_ms\": " << r.gpuP95Ms
        << ", \"completion_median_ms\": " << r.completionMedianMs
        << ", \"completion_p95_ms\": " << r.completionP95Ms
        << ", \"mutation_median_ms\": " << r.mutationMedianMs
        << ", \"mutation_p95_ms\": " << r.mutationP95Ms
        << ", \"drawlist_construction_median_ms\": "
        << r.drawListConstructionMedianMs
        << ", \"drawlist_primitive_generation_median_ms\": "
        << r.primitiveGenerationMedianMs
        << ", \"drawlist_geometry_packing_median_ms\": "
        << r.geometryPackingMedianMs
        << ", \"drawlist_command_emission_median_ms\": "
        << r.commandEmissionMedianMs
        << ", \"plan_construction_median_ms\": "
        << r.planConstructionMedianMs
        << ", \"backend_submission_median_ms\": "
        << r.backendSubmissionMedianMs
        << ", \"backend_frame_setup_median_ms\": "
        << r.backendFrameSetupMedianMs
        << ", \"backend_resource_preparation_median_ms\": "
        << r.backendResourcePreparationMedianMs
        << ", \"backend_command_execution_median_ms\": "
        << r.backendCommandExecutionMedianMs
        << ", \"backend_selection_median_ms\": "
        << r.backendSelectionMedianMs
        << ", \"submitted_draw_calls\": " << r.submittedDrawCalls
        << ", \"instanced_triangle_batches\": "
        << r.instancedTriangleBatches
        << ", \"instanced_triangle_commands\": "
        << r.instancedTriangleCommands
        << ", \"instanced_line_batches\": " << r.instancedLineBatches
        << ", \"instanced_line_commands\": " << r.instancedLineCommands
        << ", \"selection_targets\": " << r.selectionTargets
        << ", \"selection_draw_calls\": " << r.selectionDrawCalls
        << ", \"selection_instanced_batches\": "
        << r.selectionInstancedBatches
        << ", \"selection_instanced_commands\": "
        << r.selectionInstancedCommands
        << ", \"pick_draw_calls\": " << r.pickDrawCalls
        << ", \"pick_instanced_batches\": " << r.pickInstancedBatches
        << ", \"pick_instanced_commands\": " << r.pickInstancedCommands
        << ", \"pick_depth_draw_calls\": " << r.pickDepthDrawCalls
        << ", \"pick_depth_instanced_batches\": "
        << r.pickDepthInstancedBatches
        << ", \"pick_depth_instanced_commands\": "
        << r.pickDepthInstancedCommands
        << ", \"drawlist_rebuilds\": " << r.drawListRebuilds
        << ", \"incremental_command_updates\": "
        << r.incrementalCommandUpdates
        << ", \"cold_pick_ms\": " << r.coldPickMs
        << ", \"cold_pick_buffer_update_ms\": "
        << r.coldPickBufferUpdateMs
        << ", \"cold_pick_target_preparation_ms\": "
        << r.coldPickTargetPreparationMs
        << ", \"cold_pick_target_rendering_ms\": "
        << r.coldPickTargetRenderingMs
        << ", \"refresh_pick_ms\": " << r.refreshPickMs
        << ", \"refresh_pick_p95_ms\": " << r.refreshPickP95Ms
        << ", \"refresh_pick_gpu_median_ms\": "
        << r.refreshPickGpuMedianMs
        << ", \"refresh_pick_gpu_p95_ms\": " << r.refreshPickGpuP95Ms
        << ", \"refresh_pick_buffer_update_ms\": "
        << r.refreshPickBufferUpdateMs
        << ", \"refresh_pick_target_preparation_ms\": "
        << r.refreshPickTargetPreparationMs
        << ", \"refresh_pick_target_rendering_ms\": "
        << r.refreshPickTargetRenderingMs
        << ", \"refresh_pick_readback_median_ms\": "
        << r.refreshPickReadbackMedianMs
        << ", \"refresh_pick_readback_p95_ms\": "
        << r.refreshPickReadbackP95Ms
        << ", \"refresh_pick_result_resolution_median_ms\": "
        << r.refreshPickResultResolutionMedianMs
        << ", \"async_pick_request_median_ms\": "
        << r.asyncPickRequestMedianMs
        << ", \"async_pick_request_p95_ms\": "
        << r.asyncPickRequestP95Ms
        << ", \"async_pick_poll_median_ms\": "
        << r.asyncPickPollMedianMs
        << ", \"async_pick_completion_median_ms\": "
        << r.asyncPickCompletionMedianMs
        << ", \"async_pick_completion_p95_ms\": "
        << r.asyncPickCompletionP95Ms
        << ", \"pick_median_ms\": " << r.pickMedianMs
        << ", \"pick_p95_ms\": " << r.pickP95Ms
        << ", \"pick_query_median_ms\": " << r.pickQueryMedianMs
        << ", \"pick_result_resolution_median_ms\": "
        << r.pickResultResolutionMedianMs
        << ", \"pick_depth_rendering_median_ms\": "
        << r.pickDepthRenderingMedianMs
        << ", \"pick_depth_peeling_median_ms\": "
        << r.pickDepthPeelingMedianMs
        << ", \"pick_readback_median_ms\": " << r.pickReadbackMedianMs
        << ", \"pick_hit_processing_median_ms\": "
        << r.pickHitProcessingMedianMs
        << ", \"pick_target_restore_median_ms\": "
        << r.pickTargetRestoreMedianMs
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
    WorkloadKind::SharedAssemblyExpanded,
    WorkloadKind::SharedAssemblySources,
    WorkloadKind::SharedAssemblyRecipe
  };
  std::vector<Measurement> results;
  std::vector<std::string> unavailable;
  if (options.interactionOnly > 0) {
    const GLTestProfile profiles[] = {
      GLTestProfile::Compatibility, GLTestProfile::Core
    };
    for (GLTestProfile profile : profiles) {
      std::string reason;
      if (!runAssemblyMutations(
            profile, WorkloadKind::SharedAssemblyRecipe,
            options.interactionOnly, samples, results, reason, true)) {
        unavailable.push_back("shared assembly hover: " + reason);
      }
      reason.clear();
      if (!runAssemblySelectionInteractions(
            profile, options.interactionOnly, samples, results, reason)) {
        unavailable.push_back("shared assembly interactions: " + reason);
      }
      reason.clear();
      if (!runAssemblyDepthStack(
            profile, options.interactionOnly, samples, results, reason)) {
        unavailable.push_back("shared assembly depth stack: " + reason);
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
  if (options.mutationOnly > 0) {
    runMutationBenchmarks(GLTestProfile::Compatibility,
                          options.mutationOnly, samples,
                          results, unavailable);
    runMutationBenchmarks(GLTestProfile::Core, options.mutationOnly, samples,
                          results, unavailable);
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
    const WorkloadKind workload = WorkloadKind::FeatureRich;
    const int drawCount = options.rebuildOnly;
    const std::string workloadLabel = "feature_rich_rebuild_" +
      std::to_string(drawCount);
    const auto run = [&](GLTestProfile profile,
                         SoRenderManager::RenderPipeline pipeline,
                         const char * renderer,
                         bool forceRebuild) {
      Measurement measurement;
      std::string reason;
      if (runVariant(profile, pipeline, renderer, workload, drawCount, samples,
                     measurement, reason, forceRebuild)) {
        measurement.workload = workloadLabel;
        results.push_back(measurement);
      }
      else {
        unavailable.push_back(workloadLabel + ":" + renderer + ": " + reason);
      }
    };
#if COIN_HAVE_LEGACY_GL_RENDERER
    run(GLTestProfile::Compatibility,
        SoRenderManager::RenderPipeline::LEGACY_GL, "LegacyGL", false);
#endif
    run(GLTestProfile::Compatibility,
        SoRenderManager::RenderPipeline::DRAW_LIST, "DrawList", false);
    run(GLTestProfile::Compatibility,
        SoRenderManager::RenderPipeline::DRAW_LIST, "DrawList", true);
    run(GLTestProfile::Core,
        SoRenderManager::RenderPipeline::DRAW_LIST, "DrawList", false);
    run(GLTestProfile::Core,
        SoRenderManager::RenderPipeline::DRAW_LIST, "DrawList", true);

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
  for (GLTestProfile profile : { GLTestProfile::Compatibility,
                                 GLTestProfile::Core }) {
    runMutationBenchmarks(profile, draws, samples, results, unavailable);
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
