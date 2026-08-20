#include "support/GLRenderTestSession.h"
#include "support/RenderWorkloadViewerController.h"
#include "support/RenderWorkloads.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

struct Options {
  coin_test::WorkloadKind workload = coin_test::WorkloadKind::FeatureRich;
  int objects = 1000;
  GLTestProfile profile = GLTestProfile::Core;
  SoRenderManager::RenderPipeline pipeline =
    SoRenderManager::RenderPipeline::DRAW_LIST;
  int width = 1024;
  int height = 768;
  bool smoke = false;
};


void usage()
{
  std::cout
    << "Usage: CoinRenderWorkloadViewer [options]\n"
    << "  --workload NAME       Synthetic workload to display\n"
    << "  --objects N           Draws or occurrences (default: 1000)\n"
    << "  --renderer legacy|drawlist\n"
    << "  --gl-profile compat|core\n"
    << "  --size WIDTH HEIGHT\n\n"
    << "  --smoke               Hidden finite-frame integration check\n\n"
    << "Controls: wheel zoom, right/middle drag pan, left click select,\n"
    << "M mutation playback, Space pause, R rebuild, C clear, Escape exit.\n";
}

bool parseOptions(int argc, char ** argv, Options & options)
{
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      usage();
      return false;
    }
    if (arg == "--smoke") {
      options.smoke = true;
      options.workload = coin_test::WorkloadKind::SharedAssemblyRecipe;
      options.objects = 24;
      options.width = 256;
      options.height = 256;
    }
    else if (arg == "--workload" && i + 1 < argc) {
      if (!coin_test::parseWorkloadKind(argv[++i], options.workload)) {
        std::cerr << "Unknown workload: " << argv[i] << '\n';
        return false;
      }
    }
    else if (arg == "--objects" && i + 1 < argc) {
      options.objects = std::atoi(argv[++i]);
    }
    else if (arg == "--renderer" && i + 1 < argc) {
      const std::string renderer(argv[++i]);
      if (renderer == "legacy")
        options.pipeline = SoRenderManager::RenderPipeline::LEGACY_GL;
      else if (renderer == "drawlist")
        options.pipeline = SoRenderManager::RenderPipeline::DRAW_LIST;
      else {
        std::cerr << "Unknown renderer: " << renderer << '\n';
        return false;
      }
    }
    else if (arg == "--gl-profile" && i + 1 < argc) {
      const std::string profile(argv[++i]);
      if (profile == "core") options.profile = GLTestProfile::Core;
      else if (profile == "compat") options.profile = GLTestProfile::Compatibility;
      else {
        std::cerr << "Unknown GL profile: " << profile << '\n';
        return false;
      }
    }
    else if (arg == "--size" && i + 2 < argc) {
      options.width = std::atoi(argv[++i]);
      options.height = std::atoi(argv[++i]);
    }
    else {
      std::cerr << "Unknown or incomplete option: " << arg << '\n';
      return false;
    }
  }
  if (options.objects <= 0 || options.width <= 0 || options.height <= 0) {
    std::cerr << "Object count and window dimensions must be positive\n";
    return false;
  }
  if (options.pipeline == SoRenderManager::RenderPipeline::LEGACY_GL &&
      options.profile != GLTestProfile::Compatibility) {
    std::cerr << "LegacyGL requires --gl-profile compat\n";
    return false;
  }
  return true;
}

} // namespace

int main(int argc, char ** argv)
{
  Options options;
  if (!parseOptions(argc, argv, options))
    return argc > 1 && (std::string(argv[1]) == "--help" ||
                        std::string(argv[1]) == "-h") ? 0 : 2;

#if !COIN_HAVE_LEGACY_GL_RENDERER
  if (options.pipeline == SoRenderManager::RenderPipeline::LEGACY_GL) {
    std::cerr << "This build does not include LegacyGL\n";
    return 2;
  }
#endif

  SoDB::init();
  GLRenderTestConfig renderConfig;
  renderConfig.profile = options.profile;
  renderConfig.pipeline = options.pipeline;
  renderConfig.width = options.width;
  renderConfig.height = options.height;
  renderConfig.visible = !options.smoke;
  renderConfig.vsync = !options.smoke;
  GLRenderTestSession session;
  if (!session.initialize(renderConfig)) return 1;
  GLTestContext & context = session.context();
  SoRenderManager & manager = session.manager();

  SoOrthographicCamera * camera = nullptr;
  coin_test::SceneMutationHandles mutations;
  SoSeparator * scene = coin_test::makeScene(
    options.workload, options.objects, camera, &mutations);
  session.setScene(scene, camera);
  manager.setLightingMode(options.workload == coin_test::WorkloadKind::FeatureRich
                            ? SoRenderManager::LIT : SoRenderManager::UNLIT);
  manager.setBackgroundColor(SbColor4f(0.055f, 0.065f, 0.08f, 1.0f));

  coin_test::RenderWorkloadViewerController viewer(
    session, *camera, mutations, options.width, options.height);
  viewer.attach();
  if (options.smoke) {
    viewer.setAnimationEnabled(true);
    viewer.setCursorPosition(options.width * 0.5, options.height * 0.5);
  }

  std::cout << "Viewing " << coin_test::workloadName(options.workload)
            << " with " << options.objects << " objects\n"
            << "Controls: wheel zoom, right/middle drag pan, left click select, "
               "M mutate, Space pause, R rebuild, C clear\n";
  using ViewerClock = std::chrono::steady_clock;
  ViewerClock::time_point statisticsStart = ViewerClock::now();
  int renderedFrames = 0;
  int totalFrames = 0;
  const int frameLimit = options.smoke ? 12 : 0;
  while (!context.shouldClose() &&
         (frameLimit == 0 || totalFrames < frameLimit)) {
    if (!viewer.pollEvents()) continue;
    viewer.beforeRender();
    if (options.smoke && totalFrames == 3 && !viewer.resize(320, 240)) {
      std::cerr << "Viewer smoke resize failed\n";
      session.setScene(nullptr, nullptr);
      camera->unref();
      scene->unref();
      return 1;
    }
    if (options.smoke && totalFrames == 6) manager.invalidateDrawList();

    session.render();
    viewer.afterRender(
      options.pipeline == SoRenderManager::RenderPipeline::DRAW_LIST);

    context.present();
    ++renderedFrames;
    ++totalFrames;
    const ViewerClock::time_point now = ViewerClock::now();
    const double reportSeconds = std::chrono::duration<double>(
      now - statisticsStart).count();
    if (reportSeconds >= 1.0) {
      const SoRenderStatistics statistics = manager.getRenderStatistics();
      std::cout << renderedFrames / reportSeconds << " fps, "
                << statistics.drawCalls << " draws, "
                << statistics.retainedCommands << " retained commands, "
                << statistics.retainedGeometryResources << " resources\n";
      statisticsStart = now;
      renderedFrames = 0;
    }
  }

  if (options.smoke) {
    const SoRenderStatistics statistics = manager.getRenderStatistics();
    if (!manager.getLastRenderResult().rendered ||
        statistics.retainedCommands == 0 ||
        statistics.retainedGeometryResources == 0 ||
        !viewer.hasHoverTarget()) {
      std::cerr << "Viewer smoke did not complete render and hover checks\n";
      session.setScene(nullptr, nullptr);
      camera->unref();
      scene->unref();
      return 1;
    }
  }

  session.setScene(nullptr, nullptr);
  camera->unref();
  scene->unref();
  return 0;
}
