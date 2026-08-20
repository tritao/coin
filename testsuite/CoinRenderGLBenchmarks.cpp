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
  double coldPickMs = 0.0;
  double refreshPickMs = 0.0;
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
  GLuint query = 0;
  glGenQueries(1, &query);
  for (int sample = 0; sample < samples; ++sample) {
    context.bindFramebuffer();
    const Clock::time_point totalStart = Clock::now();
    glBeginQuery(GL_TIME_ELAPSED, query);
    const Clock::time_point cpuStart = Clock::now();
    manager.render(TRUE, TRUE);
    cpu.push_back(elapsedMs(cpuStart));
    glEndQuery(GL_TIME_ELAPSED);
    GLuint64 nanoseconds = 0;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
    completion.push_back(elapsedMs(totalStart));
    gpu.push_back(static_cast<double>(nanoseconds) / 1000000.0);
  }
  glDeleteQueries(1, &query);

  std::vector<double> pick;
  double coldPick = 0.0;
  double refreshPick = 0.0;
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
  if (!pick.empty()) {
    result.coldPickMs = coldPick;
    result.refreshPickMs = refreshPick;
    result.pickMedianMs = percentile(pick, 0.5);
    result.pickP95Ms = percentile(pick, 0.95);
  }
  result.pixelChecksum = pixelChecksum;
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
