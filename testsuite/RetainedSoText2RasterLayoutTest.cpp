#include "rendering/SoGLRenderBackend.h"
#include "rendering/SoRenderPlan.h"
#include "support/GLTestContext.h"

#include <Inventor/SoDB.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/system/gl.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

struct Bounds {
  int minx;
  int maxx;
  int miny;
  int maxy;
};

bool
renderText(GLTestContext & context, SoGLRenderBackend & backend,
           SoText2::Justification justification,
           Bounds & bounds)
{
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoMaterial * material = new SoMaterial;
  material->diffuseColor = SbColor(1.0f, 1.0f, 1.0f);
  root->addChild(material);

  SoFont * font = new SoFont;
  font->size = 10.0f;
  root->addChild(font);

  SoText2 * text = new SoText2;
  text->string.set1Value(0, "WIDE LINE");
  text->string.set1Value(1, "i");
  text->justification = justification;
  root->addChild(text);

  SoIRRenderAction action(SbViewportRegion(128, 64));
  action.apply(root);
  SoRenderPlanner planner;
  SoRenderPlan plan;
  SoRenderParams params = {};
  params.viewport = SbViewportRegion(128, 64);
  params.viewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(128, 64));
  params.viewMatrix.makeIdentity();
  params.projMatrix.makeIdentity();
  params.clearColor = SbVec4f(0, 0, 0, 1);
  params.clearDepth = 1.0f;
  params.flags = SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH;
  planner.build(action.getDrawList(), params.viewMatrix, plan);
  if (!backend.render(action.getDrawList(), plan, params)) {
    root->unref();
    return false;
  }
  glFinish();
  const std::vector<uint8_t> pixels = context.readPixels();
  bounds.minx = 128;
  bounds.maxx = -1;
  bounds.miny = 64;
  bounds.maxy = -1;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 128; ++x) {
      const uint8_t * pixel = &pixels[static_cast<size_t>(y * 128 + x) * 4];
      if (pixel[0] > 20 || pixel[1] > 20 || pixel[2] > 20) {
        bounds.minx = std::min(bounds.minx, x);
        bounds.maxx = std::max(bounds.maxx, x);
        bounds.miny = std::min(bounds.miny, y);
        bounds.maxy = std::max(bounds.maxy, y);
      }
    }
  }

  root->unref();
  return bounds.maxx >= bounds.minx;
}

}

int
main()
{
  SoDB::init();

  GLTestContextConfig config;
  config.profile = GLTestProfile::Core;
  config.major = 3;
  config.minor = 3;
  config.width = 128;
  config.height = 64;
  GLTestContext context;
  if (!context.initialize(config)) {
    SoDB::finish();
    return 77;
  }
  SoGLRenderBackend backend;
  SoRenderBackendInitParams init = {};
  if (!backend.initialize(init)) {
    context.shutdown();
    SoDB::finish();
    return 1;
  }

  Bounds left;
  Bounds center;
  Bounds right;
  const bool rendered =
    renderText(context, backend, SoText2::LEFT, left) &&
    renderText(context, backend, SoText2::CENTER, center) &&
    renderText(context, backend, SoText2::RIGHT, right);

  if (!rendered) {
    std::cerr << "FAIL: retained SoText2 rendering is unavailable" << std::endl;
    backend.shutdown();
    context.shutdown();
    SoDB::finish();
    return 1;
  }

  const int leftWidth = left.maxx - left.minx;
  const int centerWidth = center.maxx - center.minx;
  const int rightWidth = right.maxx - right.minx;
  const bool sameWidth =
    std::abs(leftWidth - centerWidth) <= 2 &&
    std::abs(centerWidth - rightWidth) <= 2;
  const bool aligned =
    left.minx > center.minx + 5 &&
    center.minx > right.minx + 5;

  if (!sameWidth || !aligned) {
    std::cerr << "FAIL: multiline SoText2 justification is inconsistent"
              << " (left=" << left.minx << ", center=" << center.minx
              << ", right=" << right.minx << ")" << std::endl;
    backend.shutdown();
    context.shutdown();
    SoDB::finish();
    return 1;
  }

  backend.shutdown();
  context.shutdown();
  SoDB::finish();
  return 0;
}
