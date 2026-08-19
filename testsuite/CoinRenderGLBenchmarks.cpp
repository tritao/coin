#include "support/GLTestContext.h"

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
