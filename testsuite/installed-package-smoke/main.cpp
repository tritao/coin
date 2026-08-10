#include <Inventor/SoDB.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SoSceneManager.h>
#include <Inventor/actions/SoActions.h>
#include <Inventor/elements/SoElements.h>
#include <Inventor/nodes/SoNodes.h>
#include <Inventor/system/gl.h>

// These direct includes are part of the boundary smoke test.  In a core-only
// install they must remain includable without exposing usable LegacyGL APIs.
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/caches/SoGLCacheList.h>
#include <Inventor/caches/SoGLRenderCache.h>
#include <Inventor/caches/SoPrimitiveVertexCache.h>
#include <Inventor/elements/SoGLLazyElement.h>
#include <Inventor/elements/SoGLMultiTextureEnabledElement.h>
#include <Inventor/misc/SoGLImage.h>
#include <Inventor/nodes/SoExtSelection.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/SoOffscreenRenderer.h>

#ifndef COIN_HAVE_LEGACY_GL_RENDERER
#error Missing installed renderer capability
#endif

int
main()
{
  static_assert(COIN_HAVE_LEGACY_GL_RENDERER ==
#if COIN_HAVE_LEGACY_GL_RENDERER
                1,
#else
                0,
#endif
                "installed renderer capability mismatch");

  SoDB::init();
  {
    SoRenderManager renderManager;
    SoSceneManager sceneManager;
    const SbViewportRegion viewport(SbVec2s(320, 240));
    renderManager.setViewportRegion(viewport);
    sceneManager.setViewportRegion(viewport);
    if (renderManager.getViewportRegion() != viewport ||
        sceneManager.getViewportRegion() != viewport) return 1;

#if COIN_HAVE_LEGACY_GL_RENDERER
    SoGLRenderAction action(viewport);
    SoOffscreenRenderer renderer(viewport);
    (void) action;
    (void) renderer;

    typedef void (*InitClassFunc)(void);
    InitClassFunc glRenderActionInit = &SoGLRenderAction::initClass;
    InitClassFunc glImageInit = &SoGLImage::initClass;
    InitClassFunc lazyElementInit = &SoGLLazyElement::initClass;
    InitClassFunc extSelectionInit = &SoExtSelection::initClass;
    InitClassFunc shadowGroupInit = &SoShadowGroup::initClass;
    static_assert(sizeof(SoGLRenderAction) > 0, "missing LegacyGL action");
    static_assert(sizeof(SoGLCacheList) > 0, "missing LegacyGL cache list");
    static_assert(sizeof(SoGLRenderCache) > 0, "missing LegacyGL render cache");
    static_assert(sizeof(SoPrimitiveVertexCache) > 0, "missing LegacyGL vertex cache");
    static_assert(sizeof(SoGLLazyElement) > 0, "missing LegacyGL element");
    static_assert(sizeof(SoGLImage) > 0, "missing LegacyGL image");
    static_assert(sizeof(SoExtSelection) > 0, "missing LegacyGL selection");
    static_assert(sizeof(SoShadowGroup) > 0, "missing FXViz shadow API");
    (void) glRenderActionInit;
    (void) glImageInit;
    (void) lazyElementInit;
    (void) extSelectionInit;
    (void) shadowGroupInit;
#endif
  }
  SoDB::finish();
  return 0;
}
