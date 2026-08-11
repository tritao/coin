#include <Inventor/SoDB.h>
#include <Inventor/nodes/SoNode.h>

// This translation unit intentionally uses only the public SoNode declaration.
// It must compile and link identically when the LegacyGL implementation is
// enabled or omitted from Coin.
int
main()
{
  SoDB::init();
  typedef void (SoNode::*RenderMethod)(SoGLRenderAction *);
  RenderMethod render = &SoNode::GLRender;
  RenderMethod below = &SoNode::GLRenderBelowPath;
  RenderMethod inPath = &SoNode::GLRenderInPath;
  RenderMethod offPath = &SoNode::GLRenderOffPath;
  const bool stable = render != nullptr && below != nullptr &&
    inPath != nullptr && offPath != nullptr;
  SoDB::finish();
  return stable ? 0 : 1;
}
