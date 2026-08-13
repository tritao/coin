#include <Inventor/SoDB.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/elements/SoDevicePixelRatioElement.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/SoPath.h>

#include <iostream>

static bool
sameVec3(const SbVec3f & left, const SbVec3f & right)
{
  return left[0] == right[0] && left[1] == right[1] && left[2] == right[2];
}

static int
runTest()
{
  SoDB::init();

  SoSeparator * root = new SoSeparator;
  root->ref();
  root->addChild(new SoCube);

  SoIRRenderAction action(SbViewportRegion(64, 64));
  action.beginFrame();

  SbViewportRegion viewport(100, 80);
  viewport.setViewportPixels(SbVec2s(3, 5), SbVec2s(40, 42));
  SbMatrix viewing = SbMatrix::identity();
  viewing.setTranslate(SbVec3f(2.0f, 3.0f, 4.0f));
  SbMatrix projection = SbMatrix::identity();
  projection[0][0] = 2.0f;
  SbMatrix model = SbMatrix::identity();
  model.setTranslate(SbVec3f(5.0f, 6.0f, 7.0f));

  SoViewportRegionElement::set(action.getState(), viewport);
  SoDevicePixelRatioElement::set(action.getState(), 2.0f);
  SoViewingMatrixElement::set(action.getState(), nullptr, viewing);
  SoProjectionMatrixElement::set(action.getState(), nullptr, projection);
  SoModelMatrixElement::set(action.getState(), nullptr, model);

  SoIRRenderContext context;
  context.captureFromState(action.getState());
  context.lighting.ambient.setValue(0.11f, 0.22f, 0.33f);
  context.hasLighting = TRUE;

  int result = 0;
  if (!context.hasViewport || context.viewport != viewport ||
      !context.hasViewingMatrix || context.viewingMatrix != viewing ||
      !context.hasProjectionMatrix || context.projectionMatrix != projection ||
      !context.hasModelMatrix || context.modelMatrix != model ||
      !context.hasDevicePixelRatio || context.devicePixelRatio != 2.0f) {
    std::cerr << "FAIL: replay context did not capture traversal state" << std::endl;
    result = 1;
  }

  SoViewportRegionElement::set(action.getState(), SbViewportRegion(1, 1));
  SoDevicePixelRatioElement::set(action.getState(), 1.0f);
  SoViewingMatrixElement::set(action.getState(), nullptr, SbMatrix::identity());
  SoProjectionMatrixElement::set(action.getState(), nullptr, SbMatrix::identity());
  SoModelMatrixElement::set(action.getState(), nullptr, SbMatrix::identity());
  context.applyToState(action.getState());
  if (SoViewportRegionElement::get(action.getState()) != viewport ||
      SoDevicePixelRatioElement::get(action.getState()) != 2.0f ||
      SoViewingMatrixElement::get(action.getState()) != viewing ||
      SoProjectionMatrixElement::get(action.getState()) != projection ||
      SoModelMatrixElement::get(action.getState()) != model) {
    std::cerr << "FAIL: replay context did not restore traversal state" << std::endl;
    result = 1;
  }

  action.apply(root);
  SoPath * path = new SoPath(root);
  path->ref();
  path->append(0);
  SoModelMatrixElement::set(action.getState(), nullptr, SbMatrix::identity());
  action.traverseAdditionalPath(path, context);

  if (action.getDrawList().getNumCommands() != 2) {
    std::cerr << "FAIL: replay traversal did not append a command" << std::endl;
    result = 1;
  }
  else {
    const SoRenderCommand & replay = action.getDrawList().getCommand(1);
    const SoLightingData * lighting =
      action.getDrawList().getLighting(replay.lightingHandle);
    if (replay.viewMatrix != viewing || replay.projMatrix != projection ||
        !lighting || !sameVec3(lighting->ambient, context.lighting.ambient)) {
      std::cerr << "FAIL: replay command did not use the copied context"
                << std::endl;
      result = 1;
    }
  }

  SoViewingMatrixElement::set(action.getState(), nullptr, SbMatrix::identity());
  SoProjectionMatrixElement::set(action.getState(), nullptr, SbMatrix::identity());
  SoModelMatrixElement::set(action.getState(), nullptr, SbMatrix::identity());
  action.traverseAdditionalPath(path);
  if (action.getDrawList().getNumCommands() != 3 ||
      action.getDrawList().getCommand(2).viewMatrix == viewing) {
    std::cerr << "FAIL: replay context leaked into the next traversal" << std::endl;
    result = 1;
  }

  path->unref();
  root->unref();
  return result;
}

int
main()
{
  const int result = runTest();
  SoDB::finish();
  return result;
}
