#include <Inventor/SoDB.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/SoPath.h>
#include <Inventor/actions/SoAction.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/lists/SoPathList.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSubNode.h>

#include <iostream>

class PathProbe : public SoNode {
  SO_NODE_HEADER(PathProbe);

public:
  static void initClass(void);
  PathProbe(void);

  bool sawBelowPath = false;

  void IRRender(SoIRRenderAction * action) override
  {
    this->sawBelowPath = action->getCurPathCode() == SoAction::BELOW_PATH;
  }

protected:
  ~PathProbe() override {}
};

SO_NODE_SOURCE(PathProbe);

void
PathProbe::initClass(void)
{
  SO_NODE_INIT_CLASS(PathProbe, SoNode, "SoNode");
}

PathProbe::PathProbe(void)
{
  SO_NODE_CONSTRUCTOR(PathProbe);
}

static void
check(bool condition, const char * message, int & result)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    result = 1;
  }
}

static int
runTest()
{
  PathProbe::initClass();

  SoSeparator * root = new SoSeparator;
  root->ref();
  SoCube * firstCube = new SoCube;
  SoCube * secondCube = new SoCube;
  root->addChild(firstCube);
  root->addChild(secondCube);

  PathProbe * probe = new PathProbe;
  probe->ref();

  SoIRRenderAction action(SbViewportRegion(64, 64));
  int result = 0;

  action.apply(static_cast<SoNode *>(root));
  check(action.getDrawList().getNumCommands() == 2,
        "apply(SoNode*) did not traverse both cubes", result);
  check(action.getCommandPath(0) != NULL &&
        action.getCommandPath(0)->getTail() == firstCube &&
        action.getCommandPath(1) != NULL &&
        action.getCommandPath(1)->getTail() == secondCube,
        "retained commands did not preserve their producing scene paths", result);
  action.getMutableDrawList().buildPickLUT();
  check(action.getDrawList().getPickLUT().size() >= 2,
        "pick lookup table did not assign object IDs to retained commands", result);
  const SoPickLUTEntry * firstPick = action.getDrawList().resolvePickId(1);
  check(firstPick != NULL && firstPick->commandIndex == 0,
        "pick lookup table did not resolve the first command", result);

  SoPath * firstPath = new SoPath(root);
  firstPath->append(firstCube);
  firstPath->ref();
  action.apply(firstPath);
  check(action.getDrawList().getNumCommands() == 1 &&
        action.getDrawList().getCommand(0).userData == firstCube,
        "apply(SoPath*) did not traverse the selected cube", result);
  check(action.getCommandPath(0) != NULL &&
        action.getCommandPath(0)->getTail() == firstCube,
        "path traversal did not retain the selected scene path", result);
  firstPath->unref();

  SoPath * oneNodePath = new SoPath(probe);
  oneNodePath->ref();
  action.apply(oneNodePath);
  check(probe->sawBelowPath,
        "one-node SoPath did not use BELOW_PATH traversal", result);
  check(action.getDrawList().getNumCommands() == 0,
        "one-node probe left commands in the retained frame", result);
  oneNodePath->unref();

  SoPath * firstListPath = new SoPath(root);
  firstListPath->append(firstCube);
  firstListPath->ref();
  SoPath * secondListPath = new SoPath(root);
  secondListPath->append(secondCube);
  secondListPath->ref();
  {
    SoPathList pathList;
    pathList.append(firstListPath);
    pathList.append(secondListPath);
    action.apply(pathList);
    check(action.getDrawList().getNumCommands() == 2,
          "apply(SoPathList) did not traverse both paths", result);
  }
  firstListPath->unref();
  secondListPath->unref();

  action.getMutableDrawList().addCommand(SoRenderCommand());
  action.apply(static_cast<SoNode *>(probe));
  check(action.getDrawList().getNumCommands() == 0,
        "repeated apply() did not clear the previous frame", result);

  probe->unref();
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
