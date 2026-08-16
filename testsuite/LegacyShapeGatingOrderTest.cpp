#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SbBox3f.h>
#include <Inventor/actions/SoAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/annex/FXViz/elements/SoShadowStyleElement.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/elements/SoShapeStyleElement.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {

class ShadowMapGatingShape : public SoShape {
  SO_NODE_HEADER(ShadowMapGatingShape);

public:
  static void initClass(void);
  ShadowMapGatingShape(void);

  SbBool rendered = FALSE;

  void GLRender(SoGLRenderAction * action) override {
    SoShapeStyleElement::setShadowMapRendering(action->getState(), TRUE);
    SoShadowStyleElement::set(action->getState(),
                               SoShadowStyleElement::CASTS_SHADOW);
    this->rendered = this->shouldGLRender(action);
  }

protected:
  ~ShadowMapGatingShape() override {}
  void generatePrimitives(SoAction *) override {}
  void computeBBox(SoAction *,
                   SbBox3f & box, SbVec3f & center) override {
    box.makeEmpty();
    center.setValue(0.0f, 0.0f, 0.0f);
  }
};

SO_NODE_SOURCE(ShadowMapGatingShape);

void
ShadowMapGatingShape::initClass(void)
{
  SO_NODE_INIT_CLASS(ShadowMapGatingShape, SoShape, "SoShape");
}

ShadowMapGatingShape::ShadowMapGatingShape(void)
{
  SO_NODE_CONSTRUCTOR(ShadowMapGatingShape);
}

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

int
testShadowMapDecision()
{
  ShadowMapGatingShape::initClass();

  SoSeparator * root = new SoSeparator;
  root->ref();
  ShadowMapGatingShape * shape = new ShadowMapGatingShape;
  root->addChild(shape);

  SoOffscreenRenderer renderer(SbViewportRegion(8, 8));
  renderer.setComponents(SoOffscreenRenderer::RGB);
  renderer.setBackgroundColor(SbColor(0.0f, 0.0f, 0.0f));
  if (!renderer.render(root)) {
    root->unref();
    return skip("LegacyGL offscreen rendering is unavailable");
  }

  const SbBool rendered = shape->rendered;
  root->unref();

  if (!rendered) {
    std::cerr << "FAIL: shadow-map casting shape was stopped before rendering"
              << std::endl;
    return 1;
  }
  return 0;
}

}

static int
runTest()
{
  setEnvironment("COIN_EGL", "1");
  setEnvironment("EGL_PLATFORM", "surfaceless");
  SoDB::init();

  const int shadowResult = testShadowMapDecision();

  SoSeparator * root = new SoSeparator;
  root->ref();

  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position = SbVec3f(0.0f, 0.0f, 1.0f);
  camera->height = 2.0f;
  root->addChild(camera);

  SoMaterial * material = new SoMaterial;
  material->diffuseColor = SbColor(1.0f, 0.0f, 0.0f);
  material->transparency = 0.5f;
  root->addChild(material);

  SoLightModel * lightModel = new SoLightModel;
  lightModel->model = SoLightModel::BASE_COLOR;
  root->addChild(lightModel);

  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.set1Value(0, SbVec3f(-0.7f, -0.7f, 0.0f));
  coordinates->point.set1Value(1, SbVec3f(0.7f, -0.7f, 0.0f));
  coordinates->point.set1Value(2, SbVec3f(0.0f, 0.7f, 0.0f));
  root->addChild(coordinates);
  SoFaceSet * faceSet = new SoFaceSet;
  faceSet->numVertices.set1Value(0, 3);
  root->addChild(faceSet);

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
  bool sawTransparentShape = false;
  for (int i = 0; i < 64 * 64 * 3; i += 3) {
    if (pixels[i] > 20 && pixels[i] > pixels[i + 1] + 10 &&
        pixels[i] > pixels[i + 2] + 10) {
      sawTransparentShape = true;
      break;
    }
  }

  root->unref();

  if (!sawTransparentShape) {
    std::cerr << "FAIL: transparent ordinary geometry did not survive the "
              << "sorted-triangle gating seam" << std::endl;
    return 1;
  }
  return shadowResult;
}

int
main()
{
  const int result = runTest();
  SoDB::finish();
  return result;
}
