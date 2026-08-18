#include <Inventor/SoDB.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/SoPath.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoSeparator.h>

#include <iostream>

namespace {

void check(bool condition, const char * message, int & result)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    result = 1;
  }
}

const SoRenderCommand * commandFor(const SoIRRenderAction & action,
                                   const SoNode * node,
                                   int & index)
{
  for (int i = 0; i < action.getDrawList().getNumCommands(); ++i) {
    const SoRenderCommand & command = action.getDrawList().getCommand(i);
    if (command.nodeId == node->getNodeId()) {
      index = i;
      return &command;
    }
  }
  index = -1;
  return NULL;
}

bool hasRange(const SoRenderCommand & command,
              SoPickElementType type,
              int elementIndex,
              uint32_t drawStart,
              uint32_t drawCount)
{
  for (const SoRenderElementRange & range : command.pick.elementRanges) {
    if (range.type == type && range.elementIndex == elementIndex &&
        range.drawStart == drawStart && range.drawCount == drawCount) {
      return true;
    }
  }
  return false;
}

SoCoordinate3 * coordinates(const float * values, int count)
{
  SoCoordinate3 * node = new SoCoordinate3;
  node->point.setNum(count);
  for (int i = 0; i < count; ++i) {
    node->point.set1Value(i, SbVec3f(values[i * 3], values[i * 3 + 1],
                                     values[i * 3 + 2]));
  }
  return node;
}

}

int
main()
{
  SoDB::init();

  SoSeparator * root = new SoSeparator;
  SoIndexedFaceSet * faces = new SoIndexedFaceSet;
  const float facePoints[] = {
    -1.8f, -1.0f, 0.0f, -0.2f, -1.0f, 0.0f,
     0.2f,  1.0f, 0.0f, -1.8f,  1.0f, 0.0f
  };
  int faceIndices[] = { 0, 1, 2, -1, 0, 2, 3, -1 };
  SoSeparator * faceRoot = new SoSeparator;
  faceRoot->addChild(coordinates(facePoints, 4));
  faces->coordIndex.setValues(0, 8, faceIndices);
  faceRoot->addChild(faces);
  root->addChild(faceRoot);

  SoIndexedLineSet * indexedLines = new SoIndexedLineSet;
  const float indexedLinePoints[] = {
    -0.8f, -1.0f, 0.0f, 0.8f, -1.0f, 0.0f,
    -0.8f, -0.5f, 0.0f, 0.8f, -0.5f, 0.0f
  };
  int indexedLineIndices[] = { 0, 1, -1, 2, 3, -1 };
  SoSeparator * indexedLineRoot = new SoSeparator;
  indexedLineRoot->addChild(coordinates(indexedLinePoints, 4));
  indexedLines->coordIndex.setValues(0, 6, indexedLineIndices);
  indexedLineRoot->addChild(indexedLines);
  root->addChild(indexedLineRoot);

  SoLineSet * lines = new SoLineSet;
  const float linePoints[] = {
    -0.8f, 0.0f, 0.0f, 0.8f, 0.0f, 0.0f,
    -0.8f, 0.5f, 0.0f, 0.8f, 0.5f, 0.0f
  };
  SoSeparator * lineRoot = new SoSeparator;
  lineRoot->addChild(coordinates(linePoints, 4));
  int lineCounts[] = { 2, 2 };
  lines->numVertices.setValues(0, 2, lineCounts);
  lineRoot->addChild(lines);
  root->addChild(lineRoot);

  SoPointSet * points = new SoPointSet;
  const float pointValues[] = {
    -0.7f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.7f, 1.0f, 0.0f
  };
  SoSeparator * pointRoot = new SoSeparator;
  pointRoot->addChild(coordinates(pointValues, 3));
  points->numPoints = 3;
  pointRoot->addChild(points);
  root->addChild(pointRoot);
  root->ref();

  SoIRRenderAction action(SbViewportRegion(128, 128));
  action.apply(root);

  int result = 0;
  int commandIndex = -1;
  const SoRenderCommand * faceCommand = commandFor(action, faces, commandIndex);
  check(faceCommand != NULL, "indexed faces did not produce a retained command", result);
  if (faceCommand) {
    check(hasRange(*faceCommand, SO_PICK_FACE, 0, 0, 3) &&
          hasRange(*faceCommand, SO_PICK_FACE, 1, 3, 3),
          "indexed faces did not retain face ranges", result);
    check(action.getCommandPath(commandIndex) != NULL &&
          action.getCommandPath(commandIndex)->getTail() == faces,
          "indexed face command lost its scene path", result);
  }

  const SoRenderCommand * indexedLineCommand =
    commandFor(action, indexedLines, commandIndex);
  check(indexedLineCommand != NULL,
        "indexed lines did not produce a retained command", result);
  if (indexedLineCommand) {
    check(hasRange(*indexedLineCommand, SO_PICK_EDGE, 0, 0, 2) &&
          hasRange(*indexedLineCommand, SO_PICK_EDGE, 1, 2, 2),
          "indexed lines did not retain edge ranges", result);
  }

  const SoRenderCommand * lineCommand = commandFor(action, lines, commandIndex);
  check(lineCommand != NULL, "line set did not produce a retained command", result);
  if (lineCommand) {
    check(hasRange(*lineCommand, SO_PICK_EDGE, 0, 0, 2) &&
          hasRange(*lineCommand, SO_PICK_EDGE, 1, 2, 2),
          "line set did not retain edge ranges", result);
  }

  const SoRenderCommand * pointCommand = commandFor(action, points, commandIndex);
  check(pointCommand != NULL, "point set did not produce a retained command", result);
  if (pointCommand) {
    check(hasRange(*pointCommand, SO_PICK_VERTEX, 0, 0, 1) &&
          hasRange(*pointCommand, SO_PICK_VERTEX, 1, 1, 1) &&
          hasRange(*pointCommand, SO_PICK_VERTEX, 2, 2, 1),
          "point set did not retain vertex ranges", result);
  }

  root->unref();
  SoDB::finish();
  return result;
}
