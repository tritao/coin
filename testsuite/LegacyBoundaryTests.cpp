#define COIN_INTERNAL 1

#include "CoinTest.h"

#include <Inventor/C/tidbits.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/system/gl.h>

#include "rendering/CoinOffscreenGLCanvas.h"
#include "rendering/SoGL.h"

#include <cstdio>

#ifndef GL_CONTEXT_PROFILE_MASK
#define GL_CONTEXT_PROFILE_MASK 0x9126
#endif
#ifndef GL_CONTEXT_CORE_PROFILE_BIT
#define GL_CONTEXT_CORE_PROFILE_BIT 0x00000001
#endif

struct OverlayProbe {
  int irCalls = 0;
  int glCalls = 0;
};

static SbBool
isCoreProfileContext()
{
  GLint profile = 0;
  glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
  return (profile & GL_CONTEXT_CORE_PROFILE_BIT) != 0;
}

static void
probeOverlayTraversal(void * userdata, SoAction * action)
{
  OverlayProbe * probe = static_cast<OverlayProbe *>(userdata);
  if (action->isOfType(SoIRRenderAction::getClassTypeId())) {
    ++probe->irCalls;
    SoRenderCommand & command =
      static_cast<SoIRRenderAction *>(action)->getMutableDrawList().emplaceCommand();
    command.userData = probe;
  }
  else if (action->isOfType(SoGLRenderAction::getClassTypeId())) {
    ++probe->glCalls;
  }
}

#if !defined(COIN_BUILD_LEGACY_GL_RENDERER)
BOOST_AUTO_TEST_SUITE(RetainedOverlayTests);

BOOST_AUTO_TEST_CASE(draw_list_overlays_use_ir_without_legacy_action)
{
  coin_setenv("COIN_EGL", "1", TRUE);
  coin_setenv("EGL_PLATFORM", "surfaceless", TRUE);
  coin_setenv("COIN_EGL_CORE_PROFILE", "1", TRUE);

  CoinOffscreenGLCanvas canvas;
  canvas.setWantedSize(SbVec2s(32, 32));
  const uint32_t context = canvas.activateGLContext();
  BOOST_REQUIRE(context != 0);
  BOOST_REQUIRE(isCoreProfileContext());

  SoRenderManager manager;
  manager.setViewportRegion(SbViewportRegion(32, 32));
  manager.getGLRenderAction()->setCacheContext(context);
  SoSeparator * scene = new SoSeparator;
  scene->ref();
  manager.setSceneGraph(scene);
  scene->unref();

  OverlayProbe probe;
  SoSeparator * overlay = new SoSeparator;
  overlay->ref();
  SoCallback * callback = new SoCallback;
  callback->setCallback(probeOverlayTraversal, &probe);
  overlay->addChild(callback);
  manager.addSuperimposition(overlay, 0);
  overlay->unref();

  manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
  manager.render(FALSE, FALSE);

  BOOST_CHECK_EQUAL(probe.irCalls, 1);
  BOOST_CHECK_EQUAL(probe.glCalls, 0);
  BOOST_REQUIRE(manager.getIRRenderAction());
  BOOST_REQUIRE(manager.getIRRenderAction()->getDrawList().getNumCommands() > 0);
  BOOST_CHECK(manager.getIRRenderAction()->getDrawList().getCommand(0).userData == &probe);
}

BOOST_AUTO_TEST_SUITE_END();
#endif
