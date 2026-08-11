#include <Inventor/SoDB.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoRenderLayerGroup.h>
#include <Inventor/nodes/SoSeparator.h>

#include <iostream>

int
main()
{
  SoDB::init();

  SoSeparator * root = new SoSeparator;
  SoRenderLayerGroup * layer = new SoRenderLayerGroup;
  layer->layer = SoRenderLayerGroup::FOREGROUND;
  layer->viewportOverride = TRUE;
  layer->viewportPixels.setValue(3.0f, 4.0f, 20.0f, 21.0f);
  layer->clearDepthBuffer = TRUE;
  layer->addChild(new SoCube);
  root->addChild(layer);
  root->ref();

  SoIRRenderAction action(SbViewportRegion(SbVec2s(32, 32)));
  action.apply(root);

  int result = 0;
  if (action.getDrawList().getNumCommands() != 1) {
    std::cerr << "FAIL: retained layer did not record its child" << std::endl;
    result = 1;
  }
  else {
    const SoRenderCommand & command = action.getDrawList().getCommand(0);
    if (command.stage != SoRenderStage::Foreground ||
        command.pass != SO_RENDERPASS_OVERLAY ||
        !command.state.raster.clearDepth ||
        !command.state.raster.viewportEnabled ||
        command.state.raster.viewportX != 3 ||
        command.state.raster.viewportY != 4 ||
        command.state.raster.viewportWidth != 20 ||
        command.state.raster.viewportHeight != 21) {
      std::cerr << "FAIL: retained layer placement state was not preserved" << std::endl;
      result = 1;
    }
  }

  root->unref();
  SoDB::finish();
  return result;
}
