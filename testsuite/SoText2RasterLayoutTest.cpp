#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoText2.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {

struct Bounds {
  int minx;
  int maxx;
};

void
setEnvironment(const char * name, const char * value)
{
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

bool
renderText(SoText2::Justification justification, Bounds & bounds)
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

  SoOffscreenRenderer renderer(SbViewportRegion(128, 64));
  renderer.setComponents(SoOffscreenRenderer::RGB);
  renderer.setBackgroundColor(SbColor(0.0f, 0.0f, 0.0f));
  if (!renderer.render(root)) {
    root->unref();
    return false;
  }

  const unsigned char * pixels = renderer.getBuffer();
  bounds.minx = 128;
  bounds.maxx = -1;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 128; ++x) {
      const unsigned char * pixel = pixels + (y * 128 + x) * 3;
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

int
main()
{
  setEnvironment("COIN_EGL", "1");
  setEnvironment("EGL_PLATFORM", "surfaceless");
  SoDB::init();

  Bounds left;
  Bounds center;
  Bounds right;
  const bool rendered =
    renderText(SoText2::LEFT, left) &&
    renderText(SoText2::CENTER, center) &&
    renderText(SoText2::RIGHT, right);

  if (!rendered) {
    std::cout << "SKIP: LegacyGL offscreen rendering is unavailable" << std::endl;
    SoDB::finish();
    return 77;
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
    SoDB::finish();
    return 1;
  }

  SoDB::finish();
  return 0;
}
