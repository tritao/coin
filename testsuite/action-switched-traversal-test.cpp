#include <Inventor/SoDB.h>
#include <Inventor/SoPath.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/nodes/SoSeparator.h>

#include <iostream>
#include <vector>

namespace {

struct SwitchContext {
  SoSeparator * marker = nullptr;
  SoPath * multiPath = nullptr;
  SoPath * oneNodePath = nullptr;
  SoSeparator * switchedNode = nullptr;
  bool triggered = false;
  bool multiPathVisited = false;
  bool oneNodePathVisited = false;
  bool nodeTraversalVisited = false;
  bool restored = true;
  std::vector<SoNode *> outerNodes;
  std::vector<int> outerIndices;
  SoAction::PathCode outerCode = SoAction::NO_PATH;
};

bool
samePath(const SoPath * path,
         const std::vector<SoNode *> & nodes,
         const std::vector<int> & indices)
{
  if (!path || path->getLength() != static_cast<int>(nodes.size())) return false;
  for (int i = 0; i < path->getLength(); ++i) {
    if (path->getNode(i) != nodes[static_cast<size_t>(i)] ||
        (i > 0 && path->getIndex(i) != indices[static_cast<size_t>(i)])) {
      return false;
    }
  }
  return true;
}

SoCallbackAction::Response
preCallback(void * userdata, SoCallbackAction * action, const SoNode * node)
{
  SwitchContext * context = static_cast<SwitchContext *>(userdata);
  const SoPath * current = action->getCurPath();

  if (node == context->multiPath->getTail() && !context->multiPathVisited) {
    context->multiPathVisited =
      current->getLength() == context->multiPath->getLength() &&
      samePath(current,
               { context->multiPath->getNode(0),
                 context->multiPath->getNode(1),
                 context->multiPath->getNode(2) },
               { -1,
                 context->multiPath->getIndex(1),
                 context->multiPath->getIndex(2) });
  }
  if (node == context->oneNodePath->getHead() && context->triggered &&
      !context->oneNodePathVisited) {
    context->oneNodePathVisited =
      current->getLength() == 1 &&
      action->getCurPathCode() == SoAction::BELOW_PATH;
  }
  if (node == context->switchedNode->getChild(0)) {
    context->nodeTraversalVisited = context->nodeTraversalVisited ||
      current->getLength() == 2 && current->getTail() == node &&
      action->getCurPathCode() == SoAction::NO_PATH;
  }

  if (node != context->marker || context->triggered) return SoCallbackAction::CONTINUE;

  context->triggered = true;
  context->outerCode = action->getCurPathCode();
  for (int i = 0; i < current->getLength(); ++i) {
    context->outerNodes.push_back(current->getNode(i));
    context->outerIndices.push_back(i == 0 ? -1 : current->getIndex(i));
  }

  action->switchToPathTraversal(context->multiPath);
  context->restored = context->restored &&
    action->getCurPathCode() == context->outerCode &&
    samePath(action->getCurPath(), context->outerNodes, context->outerIndices);

  action->switchToPathTraversal(context->oneNodePath);
  context->restored = context->restored &&
    action->getCurPathCode() == context->outerCode &&
    samePath(action->getCurPath(), context->outerNodes, context->outerIndices);

  action->switchToNodeTraversal(context->switchedNode);
  context->restored = context->restored &&
    action->getCurPathCode() == context->outerCode &&
    samePath(action->getCurPath(), context->outerNodes, context->outerIndices);
  return SoCallbackAction::CONTINUE;
}

}

static int
runTest()
{
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoSeparator * marker = new SoSeparator;
  SoSeparator * pathRoot = new SoSeparator;
  SoSeparator * pathGroup = new SoSeparator;
  SoSeparator * pathTail = new SoSeparator;
  pathGroup->addChild(pathTail);
  pathRoot->addChild(pathGroup);

  SoSeparator * oneNodeRoot = new SoSeparator;
  oneNodeRoot->addChild(new SoSeparator);

  SoSeparator * switchedNode = new SoSeparator;
  switchedNode->addChild(new SoSeparator);

  root->addChild(marker);
  root->addChild(pathRoot);
  root->addChild(oneNodeRoot);
  root->addChild(switchedNode);

  SoPath * multiPath = new SoPath(pathRoot);
  multiPath->append(0);
  multiPath->append(0);
  multiPath->ref();
  SoPath * oneNodePath = new SoPath(oneNodeRoot);
  oneNodePath->ref();

  SwitchContext context;
  context.marker = marker;
  context.multiPath = multiPath;
  context.oneNodePath = oneNodePath;
  context.switchedNode = switchedNode;

  SoCallbackAction action;
  action.addPreCallback(SoNode::getClassTypeId(), preCallback, &context);
  action.apply(root);

  int result = 0;
  if (!context.triggered || !context.multiPathVisited ||
      !context.oneNodePathVisited || !context.nodeTraversalVisited ||
      !context.restored) {
    std::cerr << "FAIL: switched traversal state or path lifetime is incorrect"
              << std::endl;
    result = 1;
  }

  oneNodePath->unref();
  multiPath->unref();
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
