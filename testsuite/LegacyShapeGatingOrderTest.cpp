#include <Inventor/SoDB.h>
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
#include "support/GLTestContext.h"

#include <cstdint>
#include <iostream>
#include <vector>

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

int
testShadowMapDecision(GLTestContext & context)
{
  ShadowMapGatingShape::initClass();

  SoSeparator * root = new SoSeparator;
  root->ref();
  ShadowMapGatingShape * shape = new ShadowMapGatingShape;
  root->addChild(shape);

  context.bindFramebuffer();
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  SoGLRenderAction action(SbViewportRegion(8, 8));
  action.setCacheContext(context.contextId());
  action.apply(root);

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
runTest(GLTestContext & context)
{
  const int shadowResult = testShadowMapDecision(context);

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

  context.bindFramebuffer();
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  SoGLRenderAction action(SbViewportRegion(64, 64));
  action.setCacheContext(context.contextId());
  action.setTransparencyType(
    SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
  action.apply(root);
  const std::vector<uint8_t> pixels = context.readPixels();
  bool sawTransparentShape = false;
  for (size_t i = 0; i < pixels.size(); i += 4) {
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
