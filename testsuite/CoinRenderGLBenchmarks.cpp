#include "support/GLTestContext.h"
#include "support/RenderWorkloads.h"

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

using namespace coin_test;
using Clock = std::chrono::steady_clock;

struct Options {
  bool smoke = false;
  int samples = 0;
  int rebuildOnly = 0;
  std::string output;
};

struct Measurement {
  std::string workload;
  std::string renderer;
  std::string profile;
  std::string executionMode;
  int semanticDraws = 0;
  int samples = 0;
  double cpuMedianMs = 0.0;
  double cpuP95Ms = 0.0;
  double gpuMedianMs = 0.0;
  double gpuP95Ms = 0.0;
  double completionMedianMs = 0.0;
  double completionP95Ms = 0.0;
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
  double coldPickMs = 0.0;
  double coldPickBufferUpdateMs = 0.0;
  double coldPickTargetPreparationMs = 0.0;
  double coldPickTargetRenderingMs = 0.0;
  double refreshPickMs = 0.0;
  double refreshPickBufferUpdateMs = 0.0;
  double refreshPickTargetPreparationMs = 0.0;
  double refreshPickTargetRenderingMs = 0.0;
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
  result.semanticDraws = drawCount;
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

Options parseOptions(int argc, char ** argv)
{
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--smoke") options.smoke = true;
    else if (arg == "--samples" && i + 1 < argc) options.samples = std::atoi(argv[++i]);
    else if (arg == "--rebuild-only" && i + 1 < argc)
      options.rebuildOnly = std::atoi(argv[++i]);
    else if (arg == "--output" && i + 1 < argc) options.output = argv[++i];
    else {
      std::cerr << "Usage: CoinRenderGLBenchmarks [--smoke] [--samples N] "
                   "[--rebuild-only N] [--output FILE]\n";
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
  out << "{\n  \"schema_version\": 5,\n  \"mode\": \""
      << (options.smoke ? "smoke" : "benchmark")
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
        << ", \"drawlist_rebuilds\": " << r.drawListRebuilds
        << ", \"cold_pick_ms\": " << r.coldPickMs
        << ", \"cold_pick_buffer_update_ms\": "
        << r.coldPickBufferUpdateMs
        << ", \"cold_pick_target_preparation_ms\": "
        << r.coldPickTargetPreparationMs
        << ", \"cold_pick_target_rendering_ms\": "
        << r.coldPickTargetRenderingMs
        << ", \"refresh_pick_ms\": " << r.refreshPickMs
        << ", \"refresh_pick_buffer_update_ms\": "
        << r.refreshPickBufferUpdateMs
        << ", \"refresh_pick_target_preparation_ms\": "
        << r.refreshPickTargetPreparationMs
        << ", \"refresh_pick_target_rendering_ms\": "
        << r.refreshPickTargetRenderingMs
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
    WorkloadKind::DensePicking
  };
  std::vector<Measurement> results;
  std::vector<std::string> unavailable;
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
