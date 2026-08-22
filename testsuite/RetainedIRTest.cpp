#include <Inventor/SoDB.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>

#include <cmath>
#include <iostream>

static int
runTest()
{
  SoIRRenderAction action(SbViewportRegion(64, 64));
  SoSeparator * root = new SoSeparator;
  root->ref();

  int result = 0;
  SoDrawList resourceDrawList;
  SoGeometryResource resource;
  resource.geometry.vertexCount = 3;
  resource.sourceKey = 17;
  resource.revision = 4;
  const SoGeometryHandle handle =
    resourceDrawList.addGeometryResource(resource);
  SoRenderCommand resourceCommand;
  resourceCommand.geometryHandle = handle;
  resourceDrawList.addCommand(resourceCommand);
  const SoGeometryResource * resolved =
    resourceDrawList.getGeometryResource(handle);
  if (handle == SO_INVALID_GEOMETRY_HANDLE ||
      resourceDrawList.getNumGeometryResources() != 1 ||
      !resolved || resolved->geometry.vertexCount != 3 ||
      resolved->sourceKey != 17 || resolved->revision != 4 ||
      resourceDrawList.getCommand(0).geometryHandle != handle ||
      resourceDrawList.getCommandGeometry(
        resourceDrawList.getCommand(0)).vertexCount != 3 ||
      resourceDrawList.getGeometryResource(SO_INVALID_GEOMETRY_HANDLE) ||
      resourceDrawList.getGeometryResource(handle + 1)) {
    std::cerr << "FAIL: draw-list geometry resource handle was unstable"
              << std::endl;
    result = 1;
  }
  resourceDrawList.clear();
  if (resourceDrawList.getNumGeometryResources() != 0 ||
      resourceDrawList.getGeometryResource(handle)) {
    std::cerr << "FAIL: draw-list clear retained a stale geometry resource"
              << std::endl;
    result = 1;
  }

  action.apply(root);
  if (action.getDrawList().getNumCommands() != 0) {
    std::cerr << "FAIL: empty scene emitted retained commands" << std::endl;
    result = 1;
  }

  const SbVec3f triangle[] = {
    SbVec3f(-1.0f, -1.0f, 0.0f),
    SbVec3f(1.0f, -1.0f, 0.0f),
    SbVec3f(0.0f, 1.0f, 0.0f)
  };
  SoSeparator * sharedSourceRoot = new SoSeparator;
  sharedSourceRoot->ref();
  SoCoordinate3 * sharedCoordinates = new SoCoordinate3;
  sharedCoordinates->point.setValues(0, 3, triangle);
  SoFaceSet * sharedSourceFace = new SoFaceSet;
  sharedSourceFace->numVertices.set1Value(0, 3);
  sharedSourceRoot->addChild(sharedCoordinates);
  sharedSourceRoot->addChild(sharedSourceFace);
  sharedSourceRoot->addChild(sharedSourceFace);
  action.apply(sharedSourceRoot);
  if (action.getDrawList().getNumCommands() != 2 ||
      action.getDrawList().getNumGeometryResources() != 1 ||
      action.getDrawList().getCommand(0).geometryHandle ==
        SO_INVALID_GEOMETRY_HANDLE ||
      action.getDrawList().getCommand(0).geometryHandle !=
        action.getDrawList().getCommand(1).geometryHandle) {
    std::cerr << "FAIL: repeated face-set source did not share geometry"
              << std::endl;
    result = 1;
  }
  const uint64_t originalRevision =
    action.getDrawList().getCommandGeometry(
      action.getDrawList().getCommand(0)).revision;
  sharedCoordinates->point.set1Value(0, SbVec3f(-1.5f, -1.0f, 0.0f));
  action.apply(sharedSourceRoot);
  if (action.getDrawList().getNumGeometryResources() != 1 ||
      action.getDrawList().getCommandGeometry(
        action.getDrawList().getCommand(0)).revision == originalRevision ||
      action.getDrawList().getCommand(0).geometryHandle !=
        action.getDrawList().getCommand(1).geometryHandle) {
    std::cerr << "FAIL: shared face-set source mutation was stale"
              << std::endl;
    result = 1;
  }
  const SbVec2f textureCoordinates[] = {
    SbVec2f(0.0f, 0.0f), SbVec2f(1.0f, 0.0f), SbVec2f(0.5f, 1.0f)
  };
  SoTextureCoordinate2 * sharedTexcoords = new SoTextureCoordinate2;
  sharedTexcoords->point.setValues(0, 3, textureCoordinates);
  const unsigned char texel[] = { 255, 255, 255, 255 };
  SoTexture2 * sharedTexture = new SoTexture2;
  sharedTexture->image.setValue(SbVec2s(1, 1), 4, texel);
  sharedSourceRoot->insertChild(sharedTexture, 1);
  sharedSourceRoot->insertChild(sharedTexcoords, 2);
  action.apply(sharedSourceRoot);
  if (action.getDrawList().getNumGeometryResources() != 1 ||
      action.getDrawList().getCommand(0).geometryHandle !=
        action.getDrawList().getCommand(1).geometryHandle ||
      action.getDrawList().getCommand(0).material.texture.cacheKey == 0) {
    std::cerr << "FAIL: textured face-set source did not share geometry"
              << std::endl;
    result = 1;
  }
  const uint64_t texturedRevision = action.getDrawList().getCommandGeometry(
    action.getDrawList().getCommand(0)).revision;
  sharedTexcoords->point.set1Value(0, SbVec2f(0.25f, 0.0f));
  action.apply(sharedSourceRoot);
  if (action.getDrawList().getCommandGeometry(
        action.getDrawList().getCommand(0)).revision == texturedRevision ||
      action.getDrawList().getCommand(0).geometryHandle !=
        action.getDrawList().getCommand(1).geometryHandle) {
    std::cerr << "FAIL: textured face-set coordinate mutation was stale"
              << std::endl;
    result = 1;
  }
  const SbColor vertexColors[] = {
    SbColor(1.0f, 0.0f, 0.0f), SbColor(0.0f, 1.0f, 0.0f),
    SbColor(0.0f, 0.0f, 1.0f)
  };
  SoMaterial * vertexMaterial = new SoMaterial;
  vertexMaterial->diffuseColor.setValues(0, 3, vertexColors);
  SoMaterialBinding * vertexBinding = new SoMaterialBinding;
  vertexBinding->value = SoMaterialBinding::PER_VERTEX;
  sharedSourceRoot->insertChild(vertexMaterial, 3);
  sharedSourceRoot->insertChild(vertexBinding, 4);
  action.apply(sharedSourceRoot);
  if (action.getDrawList().getNumGeometryResources() != 1 ||
      action.getDrawList().getCommand(0).geometryHandle !=
        action.getDrawList().getCommand(1).geometryHandle) {
    std::cerr << "FAIL: vertex-colored face-set source did not share geometry"
              << std::endl;
    result = 1;
  }
  const uint64_t coloredRevision = action.getDrawList().getCommandGeometry(
    action.getDrawList().getCommand(0)).revision;
  vertexMaterial->diffuseColor.set1Value(0, SbColor(0.5f, 0.25f, 0.75f));
  action.apply(sharedSourceRoot);
  if (action.getDrawList().getCommandGeometry(
        action.getDrawList().getCommand(0)).revision == coloredRevision ||
      action.getDrawList().getCommand(0).geometryHandle !=
        action.getDrawList().getCommand(1).geometryHandle) {
    std::cerr << "FAIL: vertex-color source mutation was stale" << std::endl;
    result = 1;
  }
  sharedSourceRoot->unref();

  SoSeparator * indexedSourceRoot = new SoSeparator;
  indexedSourceRoot->ref();
  SoCoordinate3 * indexedCoordinates = new SoCoordinate3;
  indexedCoordinates->point.setValues(0, 3, triangle);
  SoNormal * indexedNormals = new SoNormal;
  indexedNormals->vector.set1Value(0, SbVec3f(0.0f, 0.0f, 1.0f));
  SoNormalBinding * indexedNormalBinding = new SoNormalBinding;
  indexedNormalBinding->value = SoNormalBinding::OVERALL;
  SoIndexedFaceSet * indexedFace = new SoIndexedFaceSet;
  const int32_t indexedTriangle[] = { 0, 1, 2, -1 };
  indexedFace->coordIndex.setValues(0, 4, indexedTriangle);
  indexedSourceRoot->addChild(indexedCoordinates);
  indexedSourceRoot->addChild(indexedNormals);
  indexedSourceRoot->addChild(indexedNormalBinding);
  indexedSourceRoot->addChild(indexedFace);
  indexedSourceRoot->addChild(indexedFace);
  action.apply(indexedSourceRoot);
  if (action.getDrawList().getNumCommands() != 2 ||
      action.getDrawList().getNumGeometryResources() != 1 ||
      action.getDrawList().getCommand(0).geometryHandle !=
        action.getDrawList().getCommand(1).geometryHandle) {
    std::cerr << "FAIL: repeated indexed geometry did not share one resource"
              << std::endl;
    result = 1;
  }
  const uint64_t indexedRevision = action.getDrawList().getCommandGeometry(
    action.getDrawList().getCommand(0)).revision;
  indexedCoordinates->point.set1Value(0, SbVec3f(-1.25f, -1.0f, 0.0f));
  action.apply(indexedSourceRoot);
  if (action.getDrawList().getNumGeometryResources() != 1 ||
      action.getDrawList().getCommandGeometry(
        action.getDrawList().getCommand(0)).revision == indexedRevision ||
      action.getDrawList().getCommand(0).geometryHandle !=
        action.getDrawList().getCommand(1).geometryHandle) {
    std::cerr << "FAIL: retained indexed geometry source mutation was stale"
              << std::endl;
    result = 1;
  }
  indexedSourceRoot->unref();

  SoSeparator * indexedLineRoot = new SoSeparator;
  indexedLineRoot->ref();
  SoCoordinate3 * indexedLineCoordinates = new SoCoordinate3;
  indexedLineCoordinates->point.setValues(0, 3, triangle);
  SoIndexedLineSet * indexedLine = new SoIndexedLineSet;
  const int32_t indexedSegments[] = { 0, 1, -1, 1, 2, -1 };
  indexedLine->coordIndex.setValues(0, 6, indexedSegments);
  indexedLineRoot->addChild(indexedLineCoordinates);
  indexedLineRoot->addChild(indexedLine);
  indexedLineRoot->addChild(indexedLine);
  action.apply(indexedLineRoot);
  SbBool validIndexedLineReuse =
    action.getDrawList().getNumCommands() == 2 &&
    action.getDrawList().getNumGeometryResources() == 1;
  if (validIndexedLineReuse) {
    const SoRenderCommand & repeatedLineCommand =
      action.getDrawList().getCommand(1);
    const std::vector<SoRenderElementRange> & repeatedLineRanges =
      action.getDrawList().getCommandElementRanges(repeatedLineCommand);
    const SoGeometryResource * repeatedLineResource =
      action.getDrawList().getGeometryResource(
        repeatedLineCommand.geometryHandle);
    validIndexedLineReuse =
      action.getDrawList().getCommand(0).geometryHandle ==
        repeatedLineCommand.geometryHandle &&
      repeatedLineRanges.size() == 2 &&
      repeatedLineRanges[0].type == SO_PICK_EDGE &&
      repeatedLineRanges[0].elementIndex == 0 &&
      repeatedLineRanges[1].elementIndex == 1 &&
      repeatedLineCommand.pick.useResourceElementRanges &&
      repeatedLineCommand.pick.elementRanges.empty() &&
      repeatedLineResource && repeatedLineResource->elementRanges.size() == 2;
  }
  if (!validIndexedLineReuse) {
    std::cerr << "FAIL: repeated indexed lines did not share one resource"
              << std::endl;
    result = 1;
  }
  const uint64_t indexedLineRevision = action.getDrawList().getCommandGeometry(
    action.getDrawList().getCommand(0)).revision;
  indexedLineCoordinates->point.set1Value(0, SbVec3f(-1.1f, -1.0f, 0.0f));
  action.apply(indexedLineRoot);
  if (action.getDrawList().getNumGeometryResources() != 1 ||
      action.getDrawList().getCommandGeometry(
        action.getDrawList().getCommand(0)).revision == indexedLineRevision ||
      action.getDrawList().getCommand(0).geometryHandle !=
        action.getDrawList().getCommand(1).geometryHandle) {
    std::cerr << "FAIL: retained indexed line source mutation was stale"
              << std::endl;
    result = 1;
  }
  indexedLineRoot->unref();
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
  if (!pickEntry || pickEntry->objectId == 0) {
    std::cerr << "FAIL: retained pick entry did not carry stable object identity"
              << std::endl;
    result = 1;
  }

  SoSelectionState selection;
  SoSelectionTarget selected;
  selected.commandIndex = 0;
  selected.type = SO_PICK_OBJECT;
  selected.elementIndex = -1;
  selected.color = SbColor4f(1.0f, 0.0f, 0.0f, 0.5f);
  selection.selected.push_back(selected);
  if (selection.selected.size() != 1 ||
      selection.selected[0].commandIndex != 0 ||
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
