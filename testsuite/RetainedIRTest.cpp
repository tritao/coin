#include <Inventor/SoDB.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShaderProgram.h>

#include <cmath>
#include <iostream>

static int
runTest()
{
  SoIRRenderAction action(SbViewportRegion(64, 64));
  SoSeparator * root = new SoSeparator;
  root->ref();

  int result = 0;
  SoTextureData texture;
  if (texture.colorSpace != SO_TEXTURE_COLORSPACE_LEGACY) {
    std::cerr << "FAIL: retained textures changed legacy color interpretation"
              << std::endl;
    result = 1;
  }

  SoDrawList textureSemantics;
  SoRenderCommand linearTextureCommand;
  linearTextureCommand.material.texture.colorSpace =
    SO_TEXTURE_COLORSPACE_LINEAR;
  textureSemantics.addCommand(linearTextureCommand);
  SoRenderCommand srgbTextureCommand;
  srgbTextureCommand.material.texture.colorSpace = SO_TEXTURE_COLORSPACE_SRGB;
  textureSemantics.addCommand(srgbTextureCommand);
  if (textureSemantics.getNumCommands() != 2 ||
      textureSemantics.getCommand(0).material.texture.colorSpace !=
        SO_TEXTURE_COLORSPACE_LINEAR ||
      textureSemantics.getCommand(1).material.texture.colorSpace !=
        SO_TEXTURE_COLORSPACE_SRGB) {
    std::cerr << "FAIL: draw-list commands lost retained texture color space"
              << std::endl;
    result = 1;
  }

  action.apply(root);
  if (action.getDrawList().getNumCommands() != 0) {
    std::cerr << "FAIL: empty scene emitted retained commands" << std::endl;
    result = 1;
  }

  SoSeparator * dagRoot = new SoSeparator;
  dagRoot->ref();
  SoSeparator * parentA = new SoSeparator;
  SoSeparator * parentB = new SoSeparator;
  SoCoordinate3 * coordinatesA = new SoCoordinate3;
  SoCoordinate3 * coordinatesB = new SoCoordinate3;
  const SbVec3f triangle[] = {
    SbVec3f(-1.0f, -1.0f, 0.0f),
    SbVec3f(1.0f, -1.0f, 0.0f),
    SbVec3f(0.0f, 1.0f, 0.0f)
  };
  coordinatesA->point.setValues(0, 3, triangle);
  coordinatesB->point.setValues(0, 3, triangle);
  SoFaceSet * sharedFaceSet = new SoFaceSet;
  sharedFaceSet->numVertices.set1Value(0, 3);
  parentA->addChild(coordinatesA);
  parentA->addChild(sharedFaceSet);
  parentB->addChild(coordinatesB);
  parentB->addChild(sharedFaceSet);
  dagRoot->addChild(parentA);
  dagRoot->addChild(parentB);

  action.apply(dagRoot);
  if (action.getDrawList().getNumCommands() != 2) {
    std::cerr << "FAIL: shared DAG shape did not emit two commands"
              << std::endl;
    result = 1;
  }
  else {
    const SoRenderCommand & commandA = action.getDrawList().getCommand(0);
    const SoRenderCommand & commandB = action.getDrawList().getCommand(1);
    if (commandA.nodeId == 0 || commandA.nodeId != commandB.nodeId ||
        commandA.instanceId == 0 || commandB.instanceId == 0 ||
        commandA.instanceId == commandB.instanceId) {
      std::cerr << "FAIL: shared DAG shape occurrences conflated retained identity"
                << std::endl;
      result = 1;
    }
    if (commandA.geometry.cacheKey == 0 ||
        commandB.geometry.cacheKey == 0 ||
        commandA.geometry.cacheKey == commandB.geometry.cacheKey ||
        commandA.geometry.revision == 0 ||
        commandB.geometry.revision == 0) {
      std::cerr << "FAIL: retained geometry cache identity conflated DAG occurrences"
                << std::endl;
      result = 1;
    }
    if (commandA.geometry.resourceKey == 0 ||
        commandA.geometry.resourceKey != commandB.geometry.resourceKey) {
      std::cerr << "FAIL: identical DAG geometry did not share resource identity"
                << std::endl;
      result = 1;
    }
    const uint64_t keyA = commandA.geometry.cacheKey;
    const uint64_t keyB = commandB.geometry.cacheKey;
    const uint64_t revisionA = commandA.geometry.revision;
    const uint64_t revisionB = commandB.geometry.revision;
    const uint64_t resourceKey = commandA.geometry.resourceKey;
    action.apply(dagRoot);
    const SoRenderCommand & repeatedA = action.getDrawList().getCommand(0);
    const SoRenderCommand & repeatedB = action.getDrawList().getCommand(1);
    if (repeatedA.geometry.cacheKey != keyA ||
        repeatedB.geometry.cacheKey != keyB ||
        repeatedA.geometry.revision != revisionA ||
        repeatedB.geometry.revision != revisionB ||
        repeatedA.geometry.resourceKey != resourceKey ||
        repeatedB.geometry.resourceKey != resourceKey) {
      std::cerr << "FAIL: unchanged retained geometry identity was unstable"
                << std::endl;
      result = 1;
    }
    coordinatesA->point.set1Value(0, SbVec3f(-2.0f, -1.0f, 0.0f));
    action.apply(dagRoot);
    const SoRenderCommand & changedA = action.getDrawList().getCommand(0);
    const SoRenderCommand & changedB = action.getDrawList().getCommand(1);
    if (changedA.geometry.cacheKey != keyA ||
        changedB.geometry.cacheKey != keyB ||
        (changedA.geometry.revision == revisionA &&
         changedB.geometry.revision == revisionB)) {
      std::cerr << "FAIL: retained geometry mutation did not update revision"
                << std::endl;
      result = 1;
    }
    if (changedA.geometry.resourceKey == changedB.geometry.resourceKey) {
      std::cerr << "FAIL: distinct geometry content shared resource identity"
                << std::endl;
      result = 1;
    }
  }
  dagRoot->unref();
  action.apply(root);

  SoRenderCommand command;
  command.geometry.topology = SO_TOPOLOGY_TRIANGLES;
  command.geometry.vertexCount = 3;
  command.geometry.vertexStride = sizeof(float) * 3;
  command.material.diffuse.setValue(1.0f, 0.0f, 0.0f, 1.0f);
  action.getMutableDrawList().addCommand(command);
  if (action.getDrawList().getNumCommands() != 1 ||
      action.getDrawList().getCommand(0).geometry.vertexCount != 3) {
    std::cerr << "FAIL: retained command was not stored" << std::endl;
    result = 1;
  }

  SoSeparator * coloredRoot = new SoSeparator;
  coloredRoot->ref();
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setNum(3);
  material->diffuseColor.set1Value(0, SbColor(1.0f, 0.0f, 0.0f));
  material->diffuseColor.set1Value(1, SbColor(0.0f, 1.0f, 0.0f));
  material->diffuseColor.set1Value(2, SbColor(0.0f, 0.0f, 1.0f));
  SoMaterialBinding * binding = new SoMaterialBinding;
  binding->value = SoMaterialBinding::PER_VERTEX_INDEXED;
  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.setNum(3);
  coordinates->point.set1Value(0, SbVec3f(-1.0f, -1.0f, 0.0f));
  coordinates->point.set1Value(1, SbVec3f(1.0f, -1.0f, 0.0f));
  coordinates->point.set1Value(2, SbVec3f(0.0f, 1.0f, 0.0f));
  SoFaceSet * faceSet = new SoFaceSet;
  faceSet->numVertices.set1Value(0, 3);
  coloredRoot->addChild(material);
  coloredRoot->addChild(binding);
  coloredRoot->addChild(coordinates);
  coloredRoot->addChild(faceSet);

  action.apply(coloredRoot);
  if (action.getDrawList().getNumCommands() != 1) {
    std::cerr << "FAIL: per-vertex material scene emitted an unexpected "
              << "number of commands" << std::endl;
    result = 1;
  }
  else {
    const SoGeometryDesc & geometry =
      action.getDrawList().getCommand(0).geometry;
    const float expected[] = {
      1.0f, 0.0f, 0.0f, 1.0f,
      0.0f, 1.0f, 0.0f, 1.0f,
      0.0f, 0.0f, 1.0f, 1.0f
    };
    if (!geometry.colors || geometry.vertexCount != 3) {
      std::cerr << "FAIL: per-vertex material colors were not captured"
                << std::endl;
      result = 1;
    }
    else {
      for (int i = 0; i < 12; ++i) {
        if (std::fabs(geometry.colors[i] - expected[i]) > 0.0001f) {
          std::cerr << "FAIL: captured vertex color differs at component "
                    << i << std::endl;
          result = 1;
          break;
        }
      }
    }
  }

  coloredRoot->unref();

  action.getMutableDrawList().buildPickLUT();
  if (action.getDrawList().getPickLUT().size() != 1 ||
      action.getDrawList().resolvePickId(1) == NULL) {
    std::cerr << "FAIL: retained pick lookup table did not resolve its command"
              << std::endl;
    result = 1;
  }

  const SoPickLUTEntry * pickEntry = action.getDrawList().resolvePickId(1);
  if (!pickEntry || pickEntry->nodeId == 0 || pickEntry->instanceId == 0 ||
      pickEntry->objectId != 0) {
    std::cerr << "FAIL: retained pick entry conflated scene identities"
              << std::endl;
    result = 1;
  }

  SoSelectionState selection;
  SoSelectionTarget selected;
  selected.commandIndex = 0;
  selected.nodeId = pickEntry ? pickEntry->nodeId : 0;
  selected.instanceId = pickEntry ? pickEntry->instanceId : 0;
  selected.objectId = 0x1234;
  selected.type = SO_PICK_OBJECT;
  selected.elementIndex = -1;
  selected.color = SbColor4f(1.0f, 0.0f, 0.0f, 0.5f);
  selection.selected.push_back(selected);
  if (selection.selected.size() != 1 ||
      selection.selected[0].commandIndex != 0 ||
      selection.selected[0].nodeId != selected.nodeId ||
      selection.selected[0].instanceId != selected.instanceId ||
      selection.selected[0].objectId != selected.objectId ||
      selection.selected[0].color[0] != 1.0f) {
    std::cerr << "FAIL: selection target did not retain frame identity"
              << std::endl;
    result = 1;
  }

  action.getMutableDrawList().getCommand(0).pick.pickable = false;
  action.getMutableDrawList().buildPickLUT();
  if (!action.getDrawList().getPickLUT().empty()) {
    std::cerr << "FAIL: non-pickable command entered the pick lookup table"
              << std::endl;
    result = 1;
  }
  action.getMutableDrawList().clear();
  if (action.getDrawList().resolvePickId(1) != NULL) {
    std::cerr << "FAIL: clearing a draw list left a stale pick lookup" << std::endl;
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
