#include <Inventor/SoDB.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShaderProgram.h>

#include <iostream>

static int
runTest()
{
  SoIRRenderAction action(SbViewportRegion(64, 64));
  SoSeparator * root = new SoSeparator;
  root->ref();

  int result = 0;
  action.apply(root);
  if (action.getDrawList().getNumCommands() != 0) {
    std::cerr << "FAIL: empty scene emitted retained commands" << std::endl;
    result = 1;
  }

  SoRenderCommand command;
  const float positions[] = {
    -2.0f, 0.0f, 0.0f,
     2.0f, 0.0f, 0.0f,
     0.0f, 4.0f, 0.0f
  };
  command.geometry.topology = SO_TOPOLOGY_TRIANGLES;
  command.geometry.vertexCount = 3;
  command.geometry.positions = positions;
  command.geometry.vertexStride = sizeof(float) * 3;
  command.objectId = 42;
  command.material.diffuse.setValue(1.0f, 0.0f, 0.0f, 1.0f);
  action.addCommand(command);
  if (action.getDrawList().getNumCommands() != 1 ||
      action.getDrawList().getCommand(0).geometry.vertexCount != 3 ||
      action.getDrawList().getCommand(0).objectId != 42 ||
      !action.getDrawList().getCommand(0).geometry.hasBounds) {
    std::cerr << "FAIL: retained command was not stored" << std::endl;
    result = 1;
  }

  SoShaderProgram * shader = new SoShaderProgram;
  shader->ref();
  action.apply(shader);
  if (!action.hasUnsupportedRendering() ||
      action.getUnsupportedNode() != shader ||
      action.getUnsupportedReason() == NULL) {
    std::cerr << "FAIL: unsupported retained shader semantics were silently ignored"
              << std::endl;
    result = 1;
  }
  shader->unref();

  root->unref();
  return result;
}

int
main()
{
  SoDB::init();
  const int result = runTest();
  SoDB::finish();
  return result;
}
