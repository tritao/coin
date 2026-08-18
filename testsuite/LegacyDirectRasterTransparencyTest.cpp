#include <Inventor/SoDB.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoText2.h>

#include "support/GLTestContext.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

int
skip(const char * reason)
{
  std::cout << "SKIP: " << reason << std::endl;
  return 77;
}

}

static int
runTest(GLTestContext & context)
{
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position = SbVec3f(0.0f, 0.0f, 1.0f);
  camera->height = 2.0f;
  root->addChild(camera);

  SoMaterial * material = new SoMaterial;
  material->diffuseColor = SbColor(1.0f, 1.0f, 1.0f);
  material->transparency = 0.5f;
  root->addChild(material);

  SoText2 * text = new SoText2;
  text->string = "Coin";
  root->addChild(text);

  context.bindFramebuffer();
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  SoGLRenderAction action(SbViewportRegion(64, 64));
  action.setCacheContext(context.contextId());
  action.setTransparencyType(
    SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
  action.apply(root);
  const std::vector<uint8_t> pixels = context.readPixels();
  bool sawText = false;
  for (size_t i = 0; i < pixels.size(); i += 4) {
    if (pixels[i] > 20 || pixels[i + 1] > 20 || pixels[i + 2] > 20) {
      sawText = true;
      break;
    }
  }

  root->unref();

  if (!sawText) {
    std::cerr << "FAIL: direct-rendered transparent SoText2 was suppressed by "
              << "sorted primitive transparency" << std::endl;
    return 1;
  }
  return 0;
}

int
main()
{
  SoDB::init();
  GLTestContextConfig config;
  config.profile = GLTestProfile::Compatibility;
  config.major = 3;
  config.minor = 3;
  config.width = 64;
  config.height = 64;
  GLTestContext context;
  if (!context.initialize(config)) {
    SoDB::finish();
    return skip("LegacyGL compatibility context is unavailable");
  }
  const int result = runTest(context);
  SoDB::finish();
  return result;
}
