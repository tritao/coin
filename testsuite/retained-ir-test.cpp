#include <Inventor/SoDB.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>

#include <cassert>

int
main()
{
  SoDB::init();

  SoIRRenderAction action(SbViewportRegion(64, 64));
  SoSeparator * root = new SoSeparator;
  root->ref();
  action.apply(root);
  assert(action.getDrawList().getNumCommands() == 0);

  SoRenderCommand command;
  command.geometry.topology = SO_TOPOLOGY_TRIANGLES;
  command.geometry.vertexCount = 3;
  command.geometry.vertexStride = sizeof(float) * 3;
  command.material.diffuse.setValue(1.0f, 0.0f, 0.0f, 1.0f);
  action.getMutableDrawList().addCommand(command);
  assert(action.getDrawList().getNumCommands() == 1);
  assert(action.getDrawList().getCommand(0).geometry.vertexCount == 3);

  root->unref();
  SoDB::finish();
  return 0;
}
