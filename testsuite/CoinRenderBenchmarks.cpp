#include "rendering/SoRenderPlan.h"

#include <Inventor/SoDB.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
  bool smoke = false;
  bool stress = false;
  int frames = 0;
  int samples = 0;
  std::string output;
};

struct Result {
  std::string name;
  std::string category;
  std::string phase;
  int workload = 0;
  int samples = 0;
  double medianMs = 0.0;
  double p95Ms = 0.0;
  uint64_t checksum = 0;
};

volatile uint64_t benchmarkSink = 0;

double elapsedMs(const Clock::time_point & start)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

Result summarize(const std::string & name, const std::string & category,
                 const std::string & phase, int workload,
                 std::vector<double> timings, uint64_t checksum)
{
  std::sort(timings.begin(), timings.end());
  Result result;
  result.name = name;
  result.category = category;
  result.phase = phase;
  result.workload = workload;
  result.samples = static_cast<int>(timings.size());
  result.medianMs = timings[timings.size() / 2];
  const size_t p95 = static_cast<size_t>(
    std::ceil(static_cast<double>(timings.size()) * 0.95)) - 1;
  result.p95Ms = timings[p95];
  result.checksum = checksum;
  benchmarkSink ^= checksum;
  return result;
}

SoSeparator * makeManyDrawsScene(int drawCount, bool materialChurn)
{
  SoSeparator * root = new SoSeparator;
  root->ref();
  const SbVec3f triangle[] = {
    SbVec3f(-0.5f, -0.5f, 0.0f),
    SbVec3f(0.5f, -0.5f, 0.0f),
    SbVec3f(0.0f, 0.5f, 0.0f)
  };
  for (int i = 0; i < drawCount; ++i) {
    SoSeparator * draw = new SoSeparator;
    SoTransform * transform = new SoTransform;
    transform->translation.setValue(
      static_cast<float>(i % 100), static_cast<float>(i / 100), 0.0f);
    draw->addChild(transform);
    if (materialChurn) {
      SoMaterial * material = new SoMaterial;
      const float hue = static_cast<float>(i % 31) / 30.0f;
      material->diffuseColor.setValue(hue, 1.0f - hue, 0.5f);
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

Result benchmarkTraversal(const std::string & name, int drawCount,
                          int samples, bool materialChurn)
{
  SoSeparator * scene = makeManyDrawsScene(drawCount, materialChurn);
  SoIRRenderAction action(SbViewportRegion(64, 64));
  action.apply(scene);
  std::vector<double> timings;
  uint64_t checksum = 0;
  for (int sample = 0; sample < samples; ++sample) {
    const Clock::time_point start = Clock::now();
    action.apply(scene);
    timings.push_back(elapsedMs(start));
    checksum += static_cast<uint64_t>(action.getDrawList().getNumCommands());
  }
  scene->unref();
  return summarize(name, "render", "traversal_ir", drawCount,
                   timings, checksum);
}

SoDrawList makeDrawList(int commandCount, bool transparent,
                        int depthSegments)
{
  SoDrawList drawlist;
  drawlist.reserve(commandCount);
  for (int i = 0; i < commandCount; ++i) {
    SoRenderCommand command;
    command.objectId = static_cast<SoObjectId>(i + 1);
    command.nodeId = static_cast<SoNodeId>(i + 1);
    command.instanceId = static_cast<SoInstanceId>(i + 1);
    command.geometry.vertexCount = 3;
    command.geometry.hasBounds = TRUE;
    command.geometry.boundsCenter = SbVec3f(0.0f, 0.0f,
      -static_cast<float>((i * 7919) % std::max(commandCount, 1)));
    command.modelMatrix.makeIdentity();
    command.viewMatrix.makeIdentity();
    if (transparent) command.opacityClass = SO_OPACITY_TRANSPARENT;
    drawlist.addCommand(command);
    if (depthSegments > 1 && i > 0 &&
        i % std::max(commandCount / depthSegments, 1) == 0) {
      SoDepthClearEvent event;
      event.sequence = static_cast<uint32_t>(i);
      drawlist.addDepthClearEvent(event);
    }
  }
  return drawlist;
}

Result benchmarkPlanning(const std::string & name, const std::string & category,
                         int commandCount, int samples, bool transparent,
                         int depthSegments)
{
  const SoDrawList drawlist = makeDrawList(commandCount, transparent, depthSegments);
  SoRenderPlanner planner;
  SoRenderPlan plan;
  std::vector<double> timings;
  uint64_t checksum = 0;
  for (int sample = 0; sample < samples; ++sample) {
    const Clock::time_point start = Clock::now();
    planner.build(drawlist, plan);
    timings.push_back(elapsedMs(start));
    checksum += static_cast<uint64_t>(plan.getNumOperations());
  }
  return summarize(name, category, "plan_construction", commandCount,
                   timings, checksum);
}

Result benchmarkPicking(int commandCount, int samples)
{
  SoDrawList drawlist = makeDrawList(commandCount, false, 1);
  std::vector<double> timings;
  uint64_t checksum = 0;
  for (int sample = 0; sample < samples; ++sample) {
    drawlist.getCommand(sample % commandCount).objectId += 1;
    const Clock::time_point start = Clock::now();
    drawlist.buildPickLUT();
    for (int probe = 0; probe < 32; ++probe) {
      const uint32_t id = static_cast<uint32_t>(
        1 + ((probe * 7919 + sample) % commandCount));
      const SoPickLUTEntry * entry = drawlist.resolvePickId(id);
      if (entry) checksum += static_cast<uint64_t>(entry->commandIndex + 1);
    }
    timings.push_back(elapsedMs(start));
  }
  return summarize("PickingDenseSceneBenchmark", "picking",
                   "pick_lut_and_resolution", commandCount, timings, checksum);
}

Result benchmarkSelection(int commandCount, int selectedCount, int samples)
{
  SoDrawList drawlist = makeDrawList(commandCount, false, 4);
  std::vector<double> timings;
  uint64_t checksum = 0;
  for (int sample = 0; sample < samples; ++sample) {
    SoSelectionState & state = drawlist.getMutableSelectionState();
    const Clock::time_point start = Clock::now();
    state.selected.clear();
    state.selected.reserve(selectedCount);
    for (int i = 0; i < selectedCount; ++i) {
      SoSelectionTarget target;
      target.commandIndex = (i * 7919 + sample) % commandCount;
      target.objectId = drawlist.getCommand(target.commandIndex).objectId;
      state.selected.push_back(target);
    }
    timings.push_back(elapsedMs(start));
    checksum += state.selected.size();
  }
  return summarize("SelectionChurnBenchmark", "selection",
                   "selection_update", selectedCount, timings, checksum);
}

Result benchmarkLifecycle(int frames, int commandCount)
{
  SoDrawList drawlist;
  SoRenderPlanner planner;
  SoRenderPlan plan;
  std::vector<double> timings;
  timings.reserve(frames);
  uint64_t checksum = 0;
  for (int frame = 0; frame < frames; ++frame) {
    const Clock::time_point start = Clock::now();
    drawlist.clear();
    drawlist.reserve(commandCount);
    for (int i = 0; i < commandCount; ++i) {
      SoRenderCommand command;
      command.objectId = static_cast<SoObjectId>(frame + i + 1);
      command.nodeId = static_cast<SoNodeId>(i + 1);
      command.instanceId = static_cast<SoInstanceId>(i + 1);
      command.geometry.vertexCount = 3;
      command.opacityClass = (i % 7 == 0)
        ? SO_OPACITY_TRANSPARENT : SO_OPACITY_OPAQUE;
      drawlist.addCommand(command);
    }
    drawlist.buildPickLUT();
    SoSelectionTarget selected;
    selected.commandIndex = frame % commandCount;
    drawlist.getMutableSelectionState().selected.push_back(selected);
    planner.build(drawlist, plan);
    timings.push_back(elapsedMs(start));
    checksum += drawlist.getGeneration();
    checksum += static_cast<uint64_t>(plan.getNumOperations());
    if (!drawlist.resolvePickId(1)) {
      std::cerr << "FAIL: lifecycle stress produced a stale pick table\n";
      std::exit(1);
    }
  }
  return summarize("RenderLifecycleStressTest", "stress",
                   "frame_rebuild", commandCount, timings, checksum);
}

std::string json(const std::vector<Result> & results, const Options & options)
{
  std::ostringstream out;
  out << std::fixed << std::setprecision(6);
  out << "{\n  \"schema_version\": 1,\n"
      << "  \"mode\": \"" << (options.stress ? "stress" :
          (options.smoke ? "smoke" : "benchmark")) << "\",\n"
      << "  \"time_unit\": \"ms\",\n  \"benchmarks\": [\n";
  for (size_t i = 0; i < results.size(); ++i) {
    const Result & result = results[i];
    out << "    {\"name\": \"" << result.name
        << "\", \"category\": \"" << result.category
        << "\", \"phase\": \"" << result.phase
        << "\", \"workload\": " << result.workload
        << ", \"samples\": " << result.samples
        << ", \"median_ms\": " << result.medianMs
        << ", \"p95_ms\": " << result.p95Ms
        << ", \"checksum\": " << result.checksum << "}";
    if (i + 1 != results.size()) out << ',';
    out << '\n';
  }
  out << "  ]\n}\n";
  return out.str();
}

Options parseOptions(int argc, char ** argv)
{
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--smoke") options.smoke = true;
    else if (arg == "--stress") options.stress = true;
    else if (arg == "--frames" && i + 1 < argc) options.frames = std::atoi(argv[++i]);
    else if (arg == "--samples" && i + 1 < argc) options.samples = std::atoi(argv[++i]);
    else if (arg == "--output" && i + 1 < argc) options.output = argv[++i];
    else {
      std::cerr << "Usage: CoinRenderBenchmarks [--smoke] [--stress] "
                   "[--frames N] [--samples N] [--output FILE]\n";
      std::exit(2);
    }
  }
  if (options.smoke && options.stress) {
    std::cerr << "--smoke and --stress are mutually exclusive\n";
    std::exit(2);
  }
  return options;
}

} // namespace

int main(int argc, char ** argv)
{
  const Options options = parseOptions(argc, argv);
  SoDB::init();
  std::vector<Result> results;
  if (options.stress) {
    results.push_back(benchmarkLifecycle(
      options.frames > 0 ? options.frames : 10000, options.smoke ? 16 : 256));
  }
  else {
    const int workload = options.smoke ? 32 : 10000;
    const int traversalWorkload = options.smoke ? 8 : 1000;
    const int samples = options.samples > 0 ? options.samples : (options.smoke ? 2 : 30);
    results.push_back(benchmarkTraversal("RenderManyDrawsBenchmark",
      traversalWorkload, samples, false));
    results.push_back(benchmarkTraversal("RenderMaterialChurnBenchmark",
      traversalWorkload, samples, true));
    results.push_back(benchmarkPlanning("RenderTransparencyBenchmark", "render",
      workload, samples, true, 1));
    results.push_back(benchmarkPicking(workload, samples));
    results.push_back(benchmarkPlanning("PickingDepthStackBenchmark", "picking",
      workload, samples, false, options.smoke ? 4 : 128));
    results.push_back(benchmarkSelection(workload,
      options.smoke ? 8 : 5000, samples));
  }

  const std::string document = json(results, options);
  if (options.output.empty()) std::cout << document;
  else {
    std::ofstream output(options.output.c_str());
    if (!output) {
      std::cerr << "Unable to open benchmark output: " << options.output << '\n';
      return 1;
    }
    output << document;
  }
  return benchmarkSink == static_cast<uint64_t>(-1) ? 1 : 0;
}
