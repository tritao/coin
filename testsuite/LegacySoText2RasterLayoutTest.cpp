#include <Inventor/SoDB.h>
#include "support/GLTestContext.h"
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/system/gl.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

struct Bounds { int minx; int maxx; };

bool renderText(GLTestContext & context, SoText2::Justification justification,
                Bounds & bounds)
{
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position = SbVec3f(0.0f, 0.0f, 1.0f);
  camera->height = 2.0f;
  root->addChild(camera);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor = SbColor(1.0f, 1.0f, 1.0f);
  root->addChild(material);
  SoText2 * text = new SoText2;
  text->string.set1Value(0, "WIDE LINE");
  text->string.set1Value(1, "i");
  text->justification = justification;
  root->addChild(text);

  context.bindFramebuffer();
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  SoGLRenderAction action(SbViewportRegion(128, 64));
  action.setCacheContext(context.contextId());
  action.apply(root);
  const std::vector<uint8_t> pixels = context.readPixels();
  bounds.minx = 128;
  bounds.maxx = -1;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 128; ++x) {
      const uint8_t * pixel = &pixels[static_cast<size_t>(y * 128 + x) * 4];
      if (pixel[0] > 20 || pixel[1] > 20 || pixel[2] > 20) {
        bounds.minx = std::min(bounds.minx, x);
        bounds.maxx = std::max(bounds.maxx, x);
      }
    }
  }
  root->unref();
  return bounds.maxx >= bounds.minx;
}

}

int main()
{
  SoDB::init();
  GLTestContextConfig config;
  config.profile = GLTestProfile::Compatibility;
  config.major = 3;
  config.minor = 3;
  config.width = 128;
  config.height = 64;
  GLTestContext context;
  if (!context.initialize(config)) { SoDB::finish(); return 77; }

  Bounds left, center, right;
  const bool rendered = renderText(context, SoText2::LEFT, left) &&
    renderText(context, SoText2::CENTER, center) &&
    renderText(context, SoText2::RIGHT, right);
  if (!rendered) {
    std::cout << "SKIP: LegacyGL compatibility rendering is unavailable" << std::endl;
    SoDB::finish();
    return 77;
  }
  const int leftWidth = left.maxx - left.minx;
  const int centerWidth = center.maxx - center.minx;
  const int rightWidth = right.maxx - right.minx;
  const bool sameWidth = std::abs(leftWidth - centerWidth) <= 2 &&
    std::abs(centerWidth - rightWidth) <= 2;
  const bool aligned = left.minx > center.minx + 5 &&
    center.minx > right.minx + 5;
  if (!sameWidth || !aligned) {
    std::cerr << "FAIL: legacy multiline SoText2 justification is inconsistent"
              << std::endl;
    SoDB::finish();
    return 1;
  }
  SoDB::finish();
  return 0;
}
