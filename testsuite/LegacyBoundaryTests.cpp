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

#if defined(COIN_GL_COMPATIBILITY)
BOOST_AUTO_TEST_SUITE(LegacyBoundaryTests);

BOOST_AUTO_TEST_CASE(core_context_is_resolved_before_legacy_action_state)
{
  coin_setenv("COIN_EGL", "1", TRUE);
  coin_setenv("EGL_PLATFORM", "surfaceless", TRUE);
  coin_setenv("COIN_EGL_CORE_PROFILE", "1", TRUE);

  CoinOffscreenGLCanvas canvas;
  canvas.setWantedSize(SbVec2s(32, 32));
  BOOST_REQUIRE(canvas.activateGLContext() != 0);

  BOOST_REQUIRE(isCoreProfileContext());

  SoSeparator * scene = new SoSeparator;
  scene->ref();

  SoRenderManager manager;
  manager.setViewportRegion(SbViewportRegion(32, 32));
  manager.setSceneGraph(scene);
  manager.render(FALSE, FALSE);
  BOOST_CHECK(manager.getRenderPipeline() ==
              SoRenderManager::RenderPipeline::DRAW_LIST);

  while (glGetError() != GL_NO_ERROR) { }
  SoGLRenderAction action(SbViewportRegion(32, 32));
  action.setCacheContext(SoGLCacheContextElement::getUniqueCacheContext());
  action.apply(scene);
  BOOST_CHECK(action.hasTerminated());
  BOOST_CHECK_EQUAL(glGetError(), GL_NO_ERROR);

  scene->unref();
}

BOOST_AUTO_TEST_SUITE_END();
#endif

#if !defined(COIN_GL_COMPATIBILITY)
BOOST_AUTO_TEST_SUITE(BackendLifecycleTests);

BOOST_AUTO_TEST_CASE(draw_list_backend_retries_after_context_replacement)
{
  coin_setenv("COIN_EGL", "1", TRUE);
  coin_setenv("EGL_PLATFORM", "surfaceless", TRUE);
  coin_setenv("COIN_EGL_CORE_PROFILE", "1", TRUE);

  CoinOffscreenGLCanvas canvas;
  canvas.setWantedSize(SbVec2s(32, 32));
  const uint32_t firstContext = canvas.activateGLContext();
  BOOST_REQUIRE(firstContext != 0);
  BOOST_REQUIRE(isCoreProfileContext());

  SoRenderManager manager;
  manager.setViewportRegion(SbViewportRegion(32, 32));
  manager.getGLRenderAction()->setCacheContext(firstContext);
  SoSeparator * scene = new SoSeparator;
  scene->ref();
  manager.setSceneGraph(scene);
  scene->unref();
  manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);

  coin_setenv("COIN_TEST_FAIL_DRAW_LIST_INITIALIZATION", "1", TRUE);
  manager.render(FALSE, FALSE);
  coin_unsetenv("COIN_TEST_FAIL_DRAW_LIST_INITIALIZATION");
  BOOST_CHECK(manager.getRenderBackend() == NULL);

  canvas.deactivateGLContext();
  canvas.setWantedSize(SbVec2s(64, 64));
  const uint32_t secondContext = canvas.activateGLContext();
  BOOST_REQUIRE(secondContext != 0);
  BOOST_CHECK(secondContext != firstContext);
  BOOST_REQUIRE(isCoreProfileContext());
  manager.getGLRenderAction()->setCacheContext(secondContext);
  manager.render(FALSE, FALSE);

  BOOST_REQUIRE(manager.getRenderBackend());
}

BOOST_AUTO_TEST_SUITE_END();
#endif

#if !defined(COIN_GL_COMPATIBILITY)
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
