#include <Inventor/SoDB.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoText2.h>

#include <iostream>

static int
runTest()
{
  SoDB::init();

  int result = 0;

  SoSeparator * root = new SoSeparator;
  root->ref();
  SoText2 * text = new SoText2;
  text->string = "Coin";
  SoMaterial * material = new SoMaterial;
  material->transparency = 0.5f;
  root->addChild(material);
  root->addChild(text);

  SoDrawStyle * drawStyle = new SoDrawStyle;
  drawStyle->style = SoDrawStyle::LINES;
  drawStyle->linePattern = 0x0f0f;
  drawStyle->linePatternScaleFactor = 3;
  root->addChild(drawStyle);
  root->addChild(new SoCube);

  SoIRRenderAction action(SbViewportRegion(128, 64));
  action.apply(root);

  bool foundPixelText = false;
  bool invalidAntialiasedText = false;
  for (int i = 0; i < action.getDrawList().getNumCommands(); ++i) {
    const SoRenderCommand & command = action.getDrawList().getCommand(i);
    if (!command.pixelRaster.enabled) continue;
    const bool validCommand = command.geometry.vertexCount == 6 &&
      command.material.texture.pixels != NULL &&
      command.material.texture.width > 0 && command.material.texture.height > 0 &&
      command.opacityClass == SO_OPACITY_TRANSPARENT &&
      command.state.depth.writeEnabled;
    foundPixelText = foundPixelText || validCommand;
    if (command.state.alphaTest.reference == 0.3f) {
      invalidAntialiasedText =
        invalidAntialiasedText ||
        !command.state.blend.enabled ||
        command.state.alphaTest.policy != SO_ALPHA_TEST_POLICY_EXPLICIT;
    }
  }
  if (!foundPixelText) {
    std::cerr << "FAIL: SoText2 did not retain direct-raster depth semantics" << std::endl;
    result = 1;
  }
  if (invalidAntialiasedText) {
    std::cerr << "FAIL: SoText2 did not retain antialiased coverage semantics" << std::endl;
    result = 1;
  }

  SoSeparator * opaqueRoot = new SoSeparator;
  opaqueRoot->ref();
  SoText2 * opaqueText = new SoText2;
  opaqueText->string = "Coin";
  opaqueRoot->addChild(opaqueText);
  SoIRRenderAction opaqueAction(SbViewportRegion(128, 64));
  opaqueAction.apply(opaqueRoot);
  bool foundOpaqueText = false;
  for (int i = 0; i < opaqueAction.getDrawList().getNumCommands(); ++i) {
    const SoRenderCommand & command = opaqueAction.getDrawList().getCommand(i);
    if (command.pixelRaster.enabled &&
        command.opacityClass == SO_OPACITY_OPAQUE) {
      foundOpaqueText = true;
      break;
    }
  }
  if (!foundOpaqueText) {
    std::cerr << "FAIL: opaque SoText2 was classified as transparent" << std::endl;
    result = 1;
  }
  opaqueRoot->unref();

  int rasterWidths[2] = { 0, 0 };
  const float fontSizes[2] = { 8.0f, 16.0f };
  for (int sample = 0; sample < 2; ++sample) {
    SoSeparator * sizedRoot = new SoSeparator;
    sizedRoot->ref();
    SoFont * font = new SoFont;
    font->size = fontSizes[sample];
    sizedRoot->addChild(font);
    SoText2 * sizedText = new SoText2;
    sizedText->string = "WIDE LINE";
    sizedRoot->addChild(sizedText);
    SoIRRenderAction sizedAction(SbViewportRegion(128, 64));
    sizedAction.apply(sizedRoot);
    for (int i = 0; i < sizedAction.getDrawList().getNumCommands(); ++i) {
      const SoRenderCommand & command = sizedAction.getDrawList().getCommand(i);
      if (command.pixelRaster.enabled) {
        rasterWidths[sample] = command.material.texture.width;
        break;
      }
    }
    sizedRoot->unref();
  }
  if (rasterWidths[0] <= 0 || rasterWidths[1] <= rasterWidths[0]) {
    std::cerr << "FAIL: SoText2 retained raster did not respect font size"
              << std::endl;
    result = 1;
  }

  const unsigned char opaqueTexturePixel[] = { 255, 255, 255, 255 };
  SoSeparator * opaqueTexturedRoot = new SoSeparator;
  opaqueTexturedRoot->ref();
  SoTexture2 * opaqueTexture = new SoTexture2;
  opaqueTexture->image.setValue(SbVec2s(1, 1), 4, opaqueTexturePixel);
  opaqueTexturedRoot->addChild(opaqueTexture);
  SoText2 * opaqueTexturedText = new SoText2;
  opaqueTexturedText->string = "Coin";
  opaqueTexturedRoot->addChild(opaqueTexturedText);
  SoIRRenderAction opaqueTexturedAction(SbViewportRegion(128, 64));
  opaqueTexturedAction.apply(opaqueTexturedRoot);
  bool foundOpaqueTexturedText = false;
  for (int i = 0; i < opaqueTexturedAction.getDrawList().getNumCommands(); ++i) {
    const SoRenderCommand & command = opaqueTexturedAction.getDrawList().getCommand(i);
    if (command.pixelRaster.enabled &&
        command.opacityClass == SO_OPACITY_OPAQUE) {
      foundOpaqueTexturedText = true;
      break;
    }
  }
  if (!foundOpaqueTexturedText) {
    std::cerr << "FAIL: fully opaque RGBA texture changed SoText2 scheduling"
              << std::endl;
    result = 1;
  }
  opaqueTexturedRoot->unref();

  const unsigned char texturePixel[] = { 255, 255, 255, 128 };
  SoSeparator * texturedRoot = new SoSeparator;
  texturedRoot->ref();
  SoTexture2 * texture = new SoTexture2;
  texture->image.setValue(SbVec2s(1, 1), 4, texturePixel);
  texturedRoot->addChild(texture);
  SoText2 * texturedText = new SoText2;
  texturedText->string = "Coin";
  texturedRoot->addChild(texturedText);
  SoIRRenderAction texturedAction(SbViewportRegion(128, 64));
  texturedAction.apply(texturedRoot);
  bool foundTextureTransparentText = false;
  for (int i = 0; i < texturedAction.getDrawList().getNumCommands(); ++i) {
    const SoRenderCommand & command = texturedAction.getDrawList().getCommand(i);
    if (command.pixelRaster.enabled &&
        command.opacityClass == SO_OPACITY_TRANSPARENT) {
      foundTextureTransparentText = true;
      break;
    }
  }
  if (!foundTextureTransparentText) {
    std::cerr << "FAIL: texture transparency did not schedule SoText2 as transparent"
              << std::endl;
    result = 1;
  }
  texturedRoot->unref();

  bool foundLinePattern = false;
  for (int i = 0; i < action.getDrawList().getNumCommands(); ++i) {
    const SoRenderCommand & command = action.getDrawList().getCommand(i);
    if (command.state.raster.fillMode == SO_RASTER_LINES &&
        command.state.raster.linePattern == 0x0f0f &&
        command.state.raster.linePatternScale == 3) {
      foundLinePattern = true;
      break;
    }
  }
  if (!foundLinePattern) {
    std::cerr << "FAIL: retained line-pattern state was not captured" << std::endl;
    result = 1;
  }

  root->unref();
  return result;
}

int
main()
{
  const int result = runTest();
  SoDB::finish();
  return result;
}
