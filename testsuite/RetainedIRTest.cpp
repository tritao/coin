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

  action.getMutableDrawList().clear();
  if (action.getDrawList().getNumCommands() != 0) {
    std::cerr << "FAIL: clearing a draw list retained stale commands" << std::endl;
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
