#include <Inventor/SoDB.h>
#include <Inventor/SoPath.h>
#include <Inventor/SoLists.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSubNode.h>

#include <iostream>

class MixedTopologyShape : public SoShape {
  SO_NODE_HEADER(MixedTopologyShape);

public:
  static void initClass(void)
  {
    SO_NODE_INIT_CLASS(MixedTopologyShape, SoShape, "SoShape");
  }

  MixedTopologyShape(void)
  {
    SO_NODE_CONSTRUCTOR(MixedTopologyShape);
  }

protected:
  ~MixedTopologyShape() override {}

  void computeBBox(SoAction * action,
                   SbBox3f & box,
                   SbVec3f & center) override
  {
    (void) action;
    box.setBounds(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f);
    center.setValue(0.0f, 0.0f, 0.0f);
  }

  void generatePrimitives(SoAction * action) override
  {
    SoPrimitiveVertex a;
    SoPrimitiveVertex b;
    SoPrimitiveVertex c;
    a.setNormal(0.0f, 0.0f, 1.0f);
    b.setNormal(0.0f, 0.0f, 1.0f);
    c.setNormal(0.0f, 0.0f, 1.0f);
    a.setTextureCoords(0.0f, 0.0f);
    b.setTextureCoords(1.0f, 0.0f);
    c.setTextureCoords(0.0f, 1.0f);

    a.setPoint(-1.0f, -1.0f, 0.0f);
    b.setPoint(1.0f, -1.0f, 0.0f);
    c.setPoint(0.0f, 1.0f, 0.0f);
    this->invokeTriangleCallbacks(action, &a, &b, &c);

    a.setPoint(-1.0f, 0.0f, 0.0f);
    b.setPoint(1.0f, 0.0f, 0.0f);
    this->invokeLineSegmentCallbacks(action, &a, &b);

    a.setPoint(0.0f, 0.0f, 0.0f);
    this->invokePointCallbacks(action, &a);
  }
};

SO_NODE_SOURCE(MixedTopologyShape);

static int
runTest()
{
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoCube * cube = new SoCube;
  root->addChild(cube);
  MixedTopologyShape::initClass();
  MixedTopologyShape * mixed = new MixedTopologyShape;
  root->addChild(mixed);

  SoIRRenderAction action(SbViewportRegion(64, 64));
  action.apply(root);

  int result = 0;
  if (action.getDrawList().getNumCommands() != 4) {
    std::cerr << "FAIL: retained scene did not emit expected commands" << std::endl;
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
    const SoRenderCommand & triangle = action.getDrawList().getCommand(1);
    const SoRenderCommand & line = action.getDrawList().getCommand(2);
    const SoRenderCommand & point = action.getDrawList().getCommand(3);
    if (triangle.geometry.topology != SO_TOPOLOGY_TRIANGLES ||
        line.geometry.topology != SO_TOPOLOGY_LINES ||
        point.geometry.topology != SO_TOPOLOGY_POINTS ||
        triangle.geometry.vertexCount != 3 ||
        line.geometry.vertexCount != 2 ||
        point.geometry.vertexCount != 1 ||
        triangle.userData != mixed || line.userData != mixed ||
        point.userData != mixed) {
      std::cerr << "FAIL: mixed primitive topology was dropped or merged"
                << std::endl;
      result = 1;
    }
  }

  SoPath * path = new SoPath(root);
  path->ref();
  path->append(0);
  action.apply(path);
  if (action.getDrawList().getNumCommands() != 1 ||
      !action.getCommandPath(0) || action.getCommandPath(0)->getLength() != 2 ||
      action.getCommandPath(0)->getTail() != cube) {
    std::cerr << "FAIL: retained path application was not represented" << std::endl;
    result = 1;
  }

  SoPathList pathlist;
  pathlist.append(path);
  action.apply(pathlist);
  if (action.getDrawList().getNumCommands() != 1 ||
      !action.getCommandPath(0) || action.getCommandPath(0)->getLength() != 2) {
    std::cerr << "FAIL: retained path-list application was not represented" << std::endl;
    result = 1;
  }

  action.getMutableDrawList().addCommand(SoRenderCommand());
  action.apply(root);
  if (action.getDrawList().getNumCommands() != 4) {
    std::cerr << "FAIL: retained apply did not reset the previous frame" << std::endl;
    result = 1;
  }

  path->unref();
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
