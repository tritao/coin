// src/shapenodes/SoShapeGLRenderP.h

#ifndef COIN_SOSHAPEGLRENDERP_H
#define COIN_SOSHAPEGLRENDERP_H

#include <Inventor/SbBasic.h>

class SoGLRenderAction;
class SoShape;

// This is deliberately source-private. It separates the historical
// SoShape GL-render control flow without adding another installed rendering
// policy API for direct-rendered subclasses. The three phases preserve the
// original statement order around primitive-only transparent sorting.
struct SoShapeGLRenderContext {
  unsigned int shapeStyleFlags = 0;
  SbBool transparent = FALSE;
};

enum class SoShapeGLRenderDecision {
  Continue,
  RenderShape,
  Stop
};

class SoShapeGLRender {
public:
  // begin() performs common visibility/culling/transparency preflight.
  // RenderShape and Stop are terminal decisions; only Continue reaches the
  // primitive-sorting seam and then finish().
  static SoShapeGLRenderDecision begin(
    SoShape * shape, SoGLRenderAction * action,
    SoShapeGLRenderContext & context);
  static SbBool sortTriangles(SoShape * shape, SoGLRenderAction * action,
                              const SoShapeGLRenderContext & context);
  static SbBool finish(SoShape * shape, SoGLRenderAction * action,
                       const SoShapeGLRenderContext & context);
};

#endif // COIN_SOSHAPEGLRENDERP_H
