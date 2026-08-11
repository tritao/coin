#include <Inventor/SoDB.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoRenderLayerGroup.h>
#include <Inventor/nodes/SoSeparator.h>

#include <iostream>

static int
runTest()
{
  SoDB::init();

  SoSeparator * root = new SoSeparator;
  SoRenderLayerGroup * layer = new SoRenderLayerGroup;
  layer->layer = SoRenderLayerGroup::INHERIT;
  layer->viewportOverride = TRUE;
  layer->viewportPixels.setValue(3.0f, 4.0f, 20.0f, 21.0f);
  layer->addChild(new SoCube);
  root->addChild(layer);

  SoRenderLayerGroup * foreground = new SoRenderLayerGroup;
  foreground->layer = SoRenderLayerGroup::FOREGROUND;
  foreground->viewportOverride = TRUE;
  foreground->viewportPixels.setValue(5.0f, 6.0f, 10.0f, 11.0f);
  foreground->clearDepthBuffer = TRUE;
  SoMaterial * foregroundMaterial = new SoMaterial;
  foregroundMaterial->transparency = 0.5f;
  foreground->addChild(foregroundMaterial);
  foreground->addChild(new SoCube);
  root->addChild(foreground);

  SoRenderLayerGroup * invalid = new SoRenderLayerGroup;
  invalid->viewportOverride = TRUE;
  invalid->viewportPixels.setValue(0.0f, 0.0f, 0.0f, 11.0f);
  invalid->addChild(new SoCube);
  root->addChild(invalid);

  SoRenderLayerGroup * secondForeground = new SoRenderLayerGroup;
  secondForeground->layer = SoRenderLayerGroup::FOREGROUND;
  secondForeground->clearDepthBuffer = TRUE;
  secondForeground->addChild(new SoCube);
  root->addChild(secondForeground);
  root->ref();

  SoIRRenderAction action(SbViewportRegion(SbVec2s(32, 32)));
  action.apply(root);

  int result = 0;
  if (action.getDrawList().getNumCommands() != 3) {
    std::cerr << "FAIL: retained layer did not consistently skip invalid subtrees" << std::endl;
    result = 1;
  }
  else {
    const SoRenderCommand & main = action.getDrawList().getCommand(0);
    const SoRenderCommand & firstForeground = action.getDrawList().getCommand(1);
    const SoRenderCommand & secondForegroundCommand = action.getDrawList().getCommand(2);
    if (main.stage != SoRenderStage::Main ||
        !main.state.raster.viewportOverride ||
        !main.state.raster.viewportEnabled ||
        !main.state.useCommandMatrices ||
        main.state.raster.viewportX != 3 ||
        main.state.raster.viewportY != 4 ||
        main.state.raster.viewportWidth != 20 ||
        main.state.raster.viewportHeight != 21 ||
        firstForeground.stage != SoRenderStage::Foreground ||
        firstForeground.pass != SO_RENDERPASS_TRANSPARENT ||
        !firstForeground.state.raster.viewportOverride ||
        !firstForeground.state.useCommandMatrices ||
        firstForeground.state.raster.viewportX != 5 ||
        firstForeground.state.raster.viewportY != 6 ||
        firstForeground.state.raster.viewportWidth != 10 ||
        firstForeground.state.raster.viewportHeight != 11 ||
        secondForegroundCommand.stage != SoRenderStage::Foreground ||
        secondForegroundCommand.state.useCommandMatrices) {
      std::cerr << "FAIL: retained layer placement state was not preserved" << std::endl;
      result = 1;
    }
    const std::vector<SoDepthClearEvent> & events =
      action.getDrawList().getDepthClearEvents();
    if (events.size() != 2 ||
        events[0].stage != SoRenderStage::Foreground ||
        events[0].sequence != 1 ||
        !events[0].viewportOverride ||
        events[0].viewportX != 5 || events[0].viewportY != 6 ||
        events[0].viewportWidth != 10 || events[0].viewportHeight != 11 ||
        events[1].stage != SoRenderStage::Foreground ||
        events[1].sequence != 2 || events[1].viewportOverride) {
      std::cerr << "FAIL: retained depth-clear barriers were not preserved"
                << std::endl;
      result = 1;
    }
  }

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
