#include "support/GLRenderTestSession.h"
#include "support/RenderWorkloads.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr int viewportSize = 256;
constexpr int occurrenceCount = 64;

struct RenderedWorkload {
  std::vector<uint8_t> pixels;
  SoRenderStatistics statistics;
};

int skip(const char * reason)
{
  std::cout << "SKIP: " << reason << std::endl;
  return 77;
}

bool hasVisiblePixel(const std::vector<uint8_t> & pixels)
{
  for (size_t offset = 0; offset + 3 < pixels.size(); offset += 4) {
    if (pixels[offset] > 4 || pixels[offset + 1] > 4 ||
        pixels[offset + 2] > 4) return true;
  }
  return false;
}

bool renderWorkload(GLRenderTestSession & session, coin_test::WorkloadKind kind,
                    RenderedWorkload & result)
{
  coin_test::SceneMutationHandles mutations;
  SoOrthographicCamera * camera = nullptr;
  SoSeparator * scene = coin_test::makeScene(
    kind, occurrenceCount, camera, &mutations);

  const int definitions = coin_test::assemblyDefinitionCount(occurrenceCount);
  const bool handlesValid =
    mutations.transforms.size() == occurrenceCount &&
    mutations.materials.size() == occurrenceCount &&
    mutations.coordinates.size() == occurrenceCount &&
    mutations.definitionCoordinates.size() == static_cast<size_t>(definitions);
  if (!handlesValid) {
    std::cerr << "FAIL: " << coin_test::workloadName(kind)
              << " exposed inconsistent mutation handles" << std::endl;
    camera->unref();
    scene->unref();
    return false;
  }

  session.setScene(scene, camera);
  session.manager().setLightingMode(SoRenderManager::UNLIT);
  session.render();
  session.render();
  result.statistics = session.statistics();
  result.pixels = session.readPixels();

  const uint64_t expectedCommands = occurrenceCount * 2;
  const uint64_t sharedResources = static_cast<uint64_t>(definitions) * 2;
  const bool recipe = kind == coin_test::WorkloadKind::SharedAssemblyRecipe;
  const uint64_t minimumResources = recipe ? sharedResources : expectedCommands;
  const uint64_t maximumResources = recipe
    ? sharedResources + static_cast<uint64_t>(definitions) : expectedCommands;
  const bool structureValid =
    session.manager().getLastRenderResult().rendered &&
    session.manager().getLastRenderResult().usedPipeline ==
      SoRenderManager::RenderPipeline::DRAW_LIST &&
    result.statistics.retainedCommands == expectedCommands &&
    result.statistics.retainedGeometryResources >= minimumResources &&
    result.statistics.retainedGeometryResources <= maximumResources &&
    hasVisiblePixel(result.pixels);
  if (!structureValid) {
    std::cerr << "FAIL: " << coin_test::workloadName(kind)
              << " retained structure or output was invalid (commands="
              << result.statistics.retainedCommands << ", resources="
              << result.statistics.retainedGeometryResources << ")"
              << std::endl;
  }

  session.setScene(nullptr, nullptr);
  camera->unref();
  scene->unref();
  return structureValid;
}

bool comparePixels(const RenderedWorkload & expected,
                   const RenderedWorkload & actual,
                   const char * actualName)
{
  if (expected.pixels.size() != actual.pixels.size()) {
    std::cerr << "FAIL: " << actualName << " produced a different image size"
              << std::endl;
    return false;
  }
  size_t mismatchedPixels = 0;
  int maximumChannelDifference = 0;
  for (size_t offset = 0; offset < expected.pixels.size(); offset += 4) {
    bool pixelMismatch = false;
    for (size_t channel = 0; channel < 4; ++channel) {
      const int difference = std::abs(
        static_cast<int>(expected.pixels[offset + channel]) -
        static_cast<int>(actual.pixels[offset + channel]));
      maximumChannelDifference = std::max(maximumChannelDifference, difference);
      if (difference > 1) pixelMismatch = true;
    }
    if (pixelMismatch) ++mismatchedPixels;
  }
  if (mismatchedPixels != 0) {
    std::cerr << "FAIL: " << actualName << " differs from "
              << coin_test::workloadName(
                   coin_test::WorkloadKind::SharedAssemblyExpanded)
              << " at " << mismatchedPixels << " pixels; maximum channel "
                 "difference is " << maximumChannelDifference << std::endl;
    return false;
  }
  return true;
}

} // namespace

static int runTest()
{
  GLRenderTestConfig config;
  config.profile = GLTestProfile::Core;
  config.width = viewportSize;
  config.height = viewportSize;
  GLRenderTestSession session;
  if (!session.initialize(config)) {
    return skip("core GLFW OpenGL context is unavailable");
  }

  const coin_test::WorkloadKind kinds[] = {
    coin_test::WorkloadKind::SharedAssemblyExpanded,
    coin_test::WorkloadKind::SharedAssemblySources,
    coin_test::WorkloadKind::SharedAssemblyRecipe
  };
  RenderedWorkload rendered[3];
  bool valid = true;
  for (int i = 0; i < 3; ++i)
    valid = renderWorkload(session, kinds[i], rendered[i]) && valid;
  if (valid) {
    valid = comparePixels(rendered[0], rendered[1],
                          coin_test::workloadName(kinds[1])) && valid;
    valid = comparePixels(rendered[0], rendered[2],
                          coin_test::workloadName(kinds[2])) && valid;
  }

  return valid ? 0 : 1;
}

int main()
{
  SoDB::init();
  const int result = runTest();
  SoDB::finish();
  return result;
}
