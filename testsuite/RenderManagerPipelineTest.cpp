#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>

#include <iostream>

int
main()
{
  SoDB::init();

  int result = 0;
  {
    SoRenderManager manager;
    manager.setRenderPipeline(SoRenderManager::RenderPipeline::LEGACY_GL);

#if COIN_HAVE_LEGACY_GL_RENDERER
    if (manager.getRenderPipeline() != SoRenderManager::RenderPipeline::LEGACY_GL) {
      std::cerr << "FAIL: LegacyGL build rejected its available pipeline" << std::endl;
      result = 1;
    }
#else
    if (manager.getRenderPipeline() != SoRenderManager::RenderPipeline::DRAW_LIST) {
      std::cerr << "FAIL: core-only build accepted LEGACY_GL" << std::endl;
      result = 1;
    }
#endif

    manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
    if (manager.getRenderPipeline() != SoRenderManager::RenderPipeline::DRAW_LIST) {
      result = 1;
    }
  }
  SoDB::finish();
  return result;
}
