#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoText2.h>

#include <cstdlib>
#include <iostream>

namespace {

int
skip(const char * reason)
{
  std::cout << "SKIP: " << reason << std::endl;
  return 77;
}

void
setEnvironment(const char * name, const char * value)
{
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

}

static int
runTest()
{
  setEnvironment("COIN_EGL", "1");
  setEnvironment("EGL_PLATFORM", "surfaceless");
  SoDB::init();

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

  SoOffscreenRenderer renderer(SbViewportRegion(64, 64));
  renderer.setComponents(SoOffscreenRenderer::RGB);
  renderer.setBackgroundColor(SbColor(0.0f, 0.0f, 0.0f));
  renderer.getGLRenderAction()->setTransparencyType(
    SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);

  if (!renderer.render(root)) {
    root->unref();
    return skip("LegacyGL offscreen rendering is unavailable");
  }

  const unsigned char * pixels = renderer.getBuffer();
  bool sawText = false;
  for (int i = 0; i < 64 * 64 * 3; i += 3) {
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
  const int result = runTest();
  SoDB::finish();
  return result;
}
