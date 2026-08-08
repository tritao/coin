#include <Inventor/SoDB.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoSeparator.h>

#include <iostream>

int
main()
{
  SoDB::init();

  SoSeparator * root = new SoSeparator;
  root->ref();
  SoCube * cube = new SoCube;
  root->addChild(cube);

  SoIRRenderAction action(SbViewportRegion(64, 64));
  action.apply(root);

  int result = 0;
  if (action.getDrawList().getNumCommands() != 1) {
    std::cerr << "FAIL: cube did not emit one retained command" << std::endl;
    result = 1;
  }
  else {
    const SoRenderCommand & command = action.getDrawList().getCommand(0);
    if (command.geometry.topology != SO_TOPOLOGY_TRIANGLES ||
        command.geometry.vertexCount == 0 ||
        command.geometry.positions == NULL ||
        command.userData != cube) {
      std::cerr << "FAIL: retained cube command is incomplete" << std::endl;
      result = 1;
    }
  }

  root->unref();
  SoDB::finish();
  return result;
}
