#include "rendering/CoinOffscreenGLCanvas.h"
#include "rendering/SoGLRenderBackend.h"

#include <Inventor/SoDB.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoText2.h>

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int skip(const char * reason)
{
  std::cout << "SKIP: " << reason << std::endl;
  return 77;
}

void setEnvironment(const char * name, const char * value)
{
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

struct Fixture {
  CoinOffscreenGLCanvas canvas;
  SoGLRenderBackend backend;

  int initialize()
  {
    canvas.setWantedSize(SbVec2s(64, 64));
    if (canvas.activateGLContext() == 0) return 77;
    SoRenderBackendInitParams init = {};
    if (backend.initialize(init)) return 0;
    canvas.deactivateGLContext();
    return 1;
  }

  std::vector<uint8_t> render(const SoDrawList & drawlist,
                              const SbVec4f & clearColor,
                              float dpr = 1.0f,
                              const SbVec2s & viewportOrigin = SbVec2s(0, 0),
                              const SbVec2s & viewportSize = SbVec2s(64, 64))
  {
    SoRenderParams params = {};
    params.viewport = SbViewportRegion(64, 64);
    params.viewport.setViewportPixels(viewportOrigin, viewportSize);
    params.viewMatrix.makeIdentity();
    params.projMatrix.makeIdentity();
    params.devicePixelRatio = dpr;
    params.clearColor = clearColor;
    params.clearDepth = 1.0f;
    params.flags = SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH;
    backend.render(drawlist, params);
    glFinish();
    std::vector<uint8_t> pixels(64 * 64 * 4, 0);
    canvas.readPixels(pixels.data(), SbVec2s(64, 64), 64, 4);
    return pixels;
  }

  void shutdown()
  {
    backend.shutdown();
    canvas.deactivateGLContext();
  }
};

std::vector<uint8_t> renderNode(Fixture & fixture, SoNode * root,
                                const SbVec4f & clearColor)
{
  SoIRRenderAction action(SbViewportRegion(64, 64));
  action.apply(root);
  return fixture.render(action.getDrawList(), clearColor);
}

const uint8_t * pixelAt(const std::vector<uint8_t> & pixels, int x, int y)
{
  return &pixels[static_cast<size_t>(y * 64 + x) * 4];
}

bool check(bool condition, const char * message)
{
  if (!condition) std::cerr << "FAIL: " << message << std::endl;
  return condition;
}

SoRenderCommand coloredCommand(SoPrimitiveTopology topology,
                               const float * positions,
                               uint32_t vertexCount,
                               const SbVec4f & color)
{
  SoRenderCommand command;
  command.modelMatrix.makeIdentity();
  command.geometry.topology = topology;
  command.geometry.positions = positions;
  command.geometry.vertexCount = vertexCount;
  command.geometry.vertexStride = sizeof(float) * 3;
  command.material.diffuse = color;
  command.material.shadingModel = SO_SHADING_UNLIT;
  return command;
}

bool testWideLine(Fixture & fixture)
{
  const float positions[] = { -0.8f, 0.0f, 0.0f, 0.8f, 0.0f, 0.0f };
  SoDrawList drawlist;
  SoRenderCommand command = coloredCommand(SO_TOPOLOGY_LINES, positions, 2,
                                           SbVec4f(1, 0, 0, 1));
  command.state.raster.lineWidth = 4.0f;
  drawlist.addCommand(command);
  const std::vector<uint8_t> pixels = fixture.render(drawlist,
                                                      SbVec4f(0, 0, 0, 1), 2.0f);
  const uint8_t * center = pixelAt(pixels, 32, 32);
  const uint8_t * edge = pixelAt(pixels, 32, 35);
  return check(center[0] > 200 && center[1] < 50 &&
               edge[0] > 150 && edge[1] < 80,
               "wide-line geometry shader did not apply DPR-scaled width");
}

bool testPointSize(Fixture & fixture)
{
  const float position[] = { 0.0f, 0.0f, 0.0f };
  SoDrawList drawlist;
  SoRenderCommand command = coloredCommand(SO_TOPOLOGY_POINTS, position, 1,
                                           SbVec4f(0, 1, 0, 1));
  command.state.raster.pointSize = 12.0f;
  drawlist.addCommand(command);
  const std::vector<uint8_t> pixels = fixture.render(drawlist,
                                                      SbVec4f(0, 0, 0, 1));
  const uint8_t * center = pixelAt(pixels, 32, 32);
  return check(center[1] > 200 && center[0] < 50,
               "point-size pipeline did not render the point");
}

bool testFullLinePattern(Fixture & fixture)
{
  const float positions[] = { -0.8f, 0.0f, 0.0f, 0.8f, 0.0f, 0.0f };
  SoDrawList drawlist;
  SoRenderCommand command = coloredCommand(SO_TOPOLOGY_LINES, positions, 2,
                                           SbVec4f(1, 0, 0, 1));
  command.state.raster.lineWidth = 2.0f;
  command.state.raster.linePattern = 0x0001;
  command.state.raster.linePatternScale = 4;
  drawlist.addCommand(command);
  const std::vector<uint8_t> pixels = fixture.render(drawlist,
                                                      SbVec4f(0, 0, 0, 1));
  int redPixels = 0;
  for (int x = 8; x < 56; ++x) {
    const uint8_t * pixel = pixelAt(pixels, x, 32);
    if (pixel[0] > 180 && pixel[1] < 60) ++redPixels;
  }
  return check(redPixels > 0 && redPixels < 24,
               "wide-line shader did not apply the complete 16-bit pattern");
}

bool testLineStripPatternContinuity(Fixture & fixture)
{
  const float stripPositions[] = {
    -0.75f, 0.0f, 0.0f,
     0.0f, 0.0f, 0.0f,
     0.0f, 0.75f, 0.0f
  };
  const float separatePositions[] = {
    -0.75f, 0.0f, 0.0f,
     0.0f, 0.0f, 0.0f,
     0.0f, 0.0f, 0.0f,
     0.0f, 0.75f, 0.0f
  };
  auto render = [&](SoPrimitiveTopology topology, const float * positions,
                    uint32_t vertexCount) {
    SoDrawList drawlist;
    SoRenderCommand command = coloredCommand(
      topology, positions, vertexCount, SbVec4f(1, 0, 0, 1));
    command.state.raster.lineWidth = 2.0f;
    command.state.raster.linePattern = 0x0001;
    command.state.raster.linePatternScale = 4;
    drawlist.addCommand(command);
    return fixture.render(drawlist, SbVec4f(0, 0, 0, 1));
  };

  const std::vector<uint8_t> strip = render(
    SO_TOPOLOGY_LINE_STRIP, stripPositions, 3);
  const std::vector<uint8_t> separate = render(
    SO_TOPOLOGY_LINES, separatePositions, 4);
  int stripNearCorner = 0;
  int separateNearCorner = 0;
  for (int y = 33; y <= 37; ++y) {
    for (int x = 30; x <= 34; ++x) {
      if (pixelAt(strip, x, y)[0] > 150) ++stripNearCorner;
      if (pixelAt(separate, x, y)[0] > 150) ++separateNearCorner;
    }
  }
  return check(separateNearCorner > stripNearCorner + 2,
               "line-strip stipple restarted at a segment corner");
}

bool testIndexedLinePatternOccurrences(Fixture & fixture)
{
  const float sourcePositions[] = {
    -0.8f, -0.25f, 0.0f,
     0.0f,  0.25f, 0.0f,
     0.8f, -0.25f, 0.0f
  };
  const uint32_t lineIndices[] = { 0, 1, 1, 2 };
  const float expandedLinePositions[] = {
    -0.8f, -0.25f, 0.0f,
     0.0f,  0.25f, 0.0f,
     0.0f,  0.25f, 0.0f,
     0.8f, -0.25f, 0.0f
  };
  const float stripPositions[] = {
    -0.8f, -0.25f, 0.0f,
     0.0f,  0.25f, 0.0f,
    -0.8f, -0.25f, 0.0f
  };

  auto render = [&](SoPrimitiveTopology topology, const float * positions,
                    uint32_t vertexCount, const uint32_t * indices,
                    uint32_t indexCount) {
    SoDrawList drawlist;
    SoRenderCommand command = coloredCommand(
      topology, positions, vertexCount, SbVec4f(1, 0, 0, 1));
    command.geometry.indices = indices;
    command.geometry.indexCount = indexCount;
    command.state.raster.lineWidth = 3.0f;
    command.state.raster.linePattern = 0x0003;
    command.state.raster.linePatternScale = 5;
    drawlist.addCommand(command);
    return fixture.render(drawlist, SbVec4f(0, 0, 0, 1));
  };

  const std::vector<uint8_t> indexedLines = render(
    SO_TOPOLOGY_LINES, sourcePositions, 3, lineIndices, 4);
  const std::vector<uint8_t> expandedLines = render(
    SO_TOPOLOGY_LINES, expandedLinePositions, 4, nullptr, 0);
  if (!check(indexedLines == expandedLines,
             "indexed line stipple did not preserve per-occurrence distances")) {
    return false;
  }

  const uint32_t stripIndices[] = { 0, 1, 0 };
  const std::vector<uint8_t> indexedStrip = render(
    SO_TOPOLOGY_LINE_STRIP, sourcePositions, 3, stripIndices, 3);
  const std::vector<uint8_t> expandedStrip = render(
    SO_TOPOLOGY_LINE_STRIP, stripPositions, 3, nullptr, 0);
  return check(indexedStrip == expandedStrip,
               "indexed line-strip stipple did not preserve repeated vertices");
}

bool testEmptyLinePattern(Fixture & fixture)
{
  const float positions[] = { -0.8f, 0.0f, 0.0f, 0.8f, 0.0f, 0.0f };
  SoDrawList drawlist;
  SoRenderCommand command = coloredCommand(SO_TOPOLOGY_LINES, positions, 2,
                                           SbVec4f(1, 0, 0, 1));
  command.state.raster.lineWidth = 4.0f;
  command.state.raster.linePattern = 0x0000;
  drawlist.addCommand(command);
  const std::vector<uint8_t> pixels = fixture.render(drawlist,
                                                      SbVec4f(0, 0, 0, 1));
  const uint8_t * center = pixelAt(pixels, 32, 32);
  return check(center[0] < 20 && center[1] < 20 && center[2] < 20,
               "zero line pattern did not discard the complete line");
}

bool testTriangleFallbacks(Fixture & fixture)
{
  const float positions[] = {
    0.0f, 0.65f, 0.0f,
    -0.65f, -0.65f, 0.0f,
    0.65f, -0.65f, 0.0f
  };
  SoDrawList drawlist;
  SoRenderCommand line = coloredCommand(SO_TOPOLOGY_TRIANGLES, positions, 3,
                                        SbVec4f(1, 0, 0, 1));
  line.state.raster.fillMode = SO_RASTER_LINES;
  line.state.raster.lineWidth = 4.0f;
  drawlist.addCommand(line);
  SoRenderCommand point = line;
  point.state.raster.fillMode = SO_RASTER_POINTS;
  point.state.raster.pointSize = 12.0f;
  drawlist.addCommand(point);
  const std::vector<uint8_t> pixels = fixture.render(drawlist,
                                                      SbVec4f(0, 0, 0, 1));
  const uint8_t * top = pixelAt(pixels, 32, 52);
  const uint8_t * center = pixelAt(pixels, 32, 11);
  return check((top[0] > 150 && top[1] < 80) &&
               (center[0] > 150 && center[1] < 80),
               "triangle line/point fallbacks did not emit raster geometry");
}

int countRedPixels(const std::vector<uint8_t> & pixels)
{
  int count = 0;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      const uint8_t * pixel = pixelAt(pixels, x, y);
      if (pixel[0] > 150 && pixel[1] < 80 && pixel[2] < 80) ++count;
    }
  }
  return count;
}

int countGreenPixels(const std::vector<uint8_t> & pixels)
{
  int count = 0;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      const uint8_t * pixel = pixelAt(pixels, x, y);
      if (pixel[1] > 150 && pixel[0] < 80 && pixel[2] < 80) ++count;
    }
  }
  return count;
}

bool testPatternedTriangleFallback(Fixture & fixture)
{
  const float positions[] = {
    0.0f, 0.65f, 0.0f,
    -0.65f, -0.65f, 0.0f,
    0.65f, -0.65f, 0.0f
  };
  auto renderPattern = [&](uint16_t pattern) {
    SoDrawList drawlist;
    SoRenderCommand command = coloredCommand(
      SO_TOPOLOGY_TRIANGLES, positions, 3, SbVec4f(1, 0, 0, 1));
    command.state.raster.fillMode = SO_RASTER_LINES;
    command.state.raster.lineWidth = 4.0f;
    command.state.raster.linePattern = pattern;
    command.state.raster.linePatternScale = 4;
    drawlist.addCommand(command);
    return fixture.render(drawlist, SbVec4f(0, 0, 0, 1));
  };

  const int patterned = countRedPixels(renderPattern(0x0001));
  const int solid = countRedPixels(renderPattern(0xFFFF));
  return check(patterned > 0 && patterned < solid / 2,
               "triangle wireframe fallback did not vary stipple along edges");
}

bool testTriangleFallbackCulling(Fixture & fixture)
{
  const float frontPositions[] = {
    -0.85f, -0.55f, 0.0f,
    -0.25f, -0.55f, 0.0f,
    -0.55f,  0.55f, 0.0f
  };
  const float backPositions[] = {
     0.85f, -0.55f, 0.0f,
     0.25f, -0.55f, 0.0f,
     0.55f,  0.55f, 0.0f
  };

  auto render = [&](bool frontFaceCCW, bool points) {
    SoDrawList drawlist;
    SoRenderCommand front = coloredCommand(
      SO_TOPOLOGY_TRIANGLES, frontPositions, 3, SbVec4f(1, 0, 0, 1));
    SoRenderCommand back = coloredCommand(
      SO_TOPOLOGY_TRIANGLES, backPositions, 3, SbVec4f(0, 1, 0, 1));
    for (SoRenderCommand * command : {&front, &back}) {
      command->state.raster.cullBackFaces = TRUE;
      command->state.raster.frontFaceCCW = frontFaceCCW ? TRUE : FALSE;
      if (points) {
        command->state.raster.fillMode = SO_RASTER_POINTS;
        command->state.raster.pointSize = 512.0f;
      }
      else {
        command->state.raster.fillMode = SO_RASTER_LINES;
        command->state.raster.lineWidth = 4.0f;
        // A nearly solid pattern forces the triangle line fallback without
        // requiring a test-specific assumption about the native line range.
        command->state.raster.linePattern = 0xFFFE;
      }
    }
    drawlist.addCommand(front);
    drawlist.addCommand(back);
    return fixture.render(drawlist, SbVec4f(0, 0, 0, 1));
  };

  const std::vector<uint8_t> ccwLines = render(true, false);
  if (!check(countRedPixels(ccwLines) > 0 && countGreenPixels(ccwLines) == 0,
             "triangle line fallback did not cull a back-facing source triangle")) {
    return false;
  }
  const std::vector<uint8_t> cwLines = render(false, false);
  if (!check(countRedPixels(cwLines) == 0 && countGreenPixels(cwLines) > 0,
             "triangle line fallback did not honor clockwise front faces")) {
    return false;
  }

  const std::vector<uint8_t> ccwPoints = render(true, true);
  if (!check(countRedPixels(ccwPoints) > 0 && countGreenPixels(ccwPoints) == 0,
             "triangle point fallback did not cull a back-facing source triangle")) {
    return false;
  }
  const std::vector<uint8_t> cwPoints = render(false, true);
  return check(countRedPixels(cwPoints) == 0 && countGreenPixels(cwPoints) > 0,
               "triangle point fallback did not honor clockwise front faces");
}

bool testTriangleStripFallbackCulling(Fixture & fixture)
{
  const float positions[] = {
    -0.7f, -0.7f, 0.0f,
     0.7f, -0.7f, 0.0f,
    -0.7f,  0.7f, 0.0f,
     0.7f,  0.7f, 0.0f
  };
  SoRenderCommand command = coloredCommand(
    SO_TOPOLOGY_TRIANGLE_STRIP, positions, 4, SbVec4f(1, 0, 0, 1));
  command.state.raster.cullBackFaces = TRUE;
  command.state.raster.frontFaceCCW = TRUE;
  command.state.raster.fillMode = SO_RASTER_LINES;
  command.state.raster.lineWidth = 4.0f;
  command.state.raster.linePattern = 0xFFFD;
  SoDrawList drawlist;
  drawlist.addCommand(command);
  const std::vector<uint8_t> pixels = fixture.render(
    drawlist, SbVec4f(0, 0, 0, 1));
  const uint8_t * bottom = pixelAt(pixels, 32, 10);
  const uint8_t * top = pixelAt(pixels, 32, 54);
  return check(bottom[0] > 150 && top[0] > 150,
               "triangle-strip fallback did not preserve source-face parity");
}

bool testPolygonOffsetTargets(Fixture & fixture)
{
  const float positions[] = {
    -0.6f, -0.6f, 0.0f,
     0.6f, -0.6f, 0.0f,
     0.6f,  0.6f, 0.0f,
    -0.6f, -0.6f, 0.0f,
     0.6f,  0.6f, 0.0f,
    -0.6f,  0.6f, 0.0f
  };

  auto render = [&](SoRasterFillMode mode) {
    SoDrawList drawlist;
    SoRenderCommand base = coloredCommand(
      SO_TOPOLOGY_TRIANGLES, positions, 6, SbVec4f(0, 0, 1, 1));
    base.state.depth.func = SO_DEPTH_LESS;
    SoRenderCommand overlay = coloredCommand(
      SO_TOPOLOGY_TRIANGLES, positions, 6, SbVec4f(1, 0, 0, 1));
    overlay.state.depth.func = SO_DEPTH_LESS;
    overlay.state.raster.fillMode = mode;
    overlay.state.raster.polygonOffsetFactor = -1.0f;
    overlay.state.raster.polygonOffsetUnits = -1.0f;
    if (mode == SO_RASTER_FILL) {
      overlay.state.raster.polygonOffsetFilled = TRUE;
    }
    else if (mode == SO_RASTER_LINES) {
      overlay.state.raster.polygonOffsetLines = TRUE;
      overlay.state.raster.lineWidth = 8.0f;
      overlay.state.raster.linePattern = 0xFFFE;
    }
    else {
      overlay.state.raster.polygonOffsetPoints = TRUE;
      overlay.state.raster.pointSize = 32.0f;
    }
    drawlist.addCommand(base);
    drawlist.addCommand(overlay);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glEnable(GL_POLYGON_OFFSET_POINT);
    glPolygonOffset(100.0f, 100.0f);
    return fixture.render(drawlist, SbVec4f(0, 0, 0, 1));
  };

  const std::vector<uint8_t> filled = render(SO_RASTER_FILL);
  if (!check(countRedPixels(filled) > 0,
             "filled polygon offset did not move the overlay forward")) {
    return false;
  }
  const std::vector<uint8_t> lines = render(SO_RASTER_LINES);
  if (!check(countRedPixels(lines) > 0,
             "line polygon offset did not cover emulated line geometry")) {
    return false;
  }
  const std::vector<uint8_t> points = render(SO_RASTER_POINTS);
  return check(countRedPixels(points) > 0,
               "point polygon offset did not cover emulated point geometry");
}

bool testSemanticFallback(Fixture & fixture)
{
  const unsigned char texturePixels[] = { 255, 0, 0, 255 };
  const float linePositions[] = {
    -0.75f, 0.0f, 0.0f, 0.75f, 0.0f, 0.0f
  };
  const float lineTexcoords[] = {
    0.5f, 0.5f, 0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f
  };
  const float pointPosition[] = { 0.0f, 0.0f, 0.0f };
  const float pointTexcoord[] = { 0.5f, 0.5f, 0.0f, 0.0f };

  auto render = [&](SoPrimitiveTopology topology, const float * positions,
                    uint32_t vertexCount, const float * texcoords) {
    SoDrawList drawlist;
    SoRenderCommand command = coloredCommand(
      topology, positions, vertexCount, SbVec4f(1, 1, 1, 0.5f));
    command.geometry.texcoords = texcoords;
    command.geometry.texcoordStride = sizeof(float) * 4;
    command.material.texture.pixels = texturePixels;
    command.material.texture.width = 1;
    command.material.texture.height = 1;
    command.material.texture.numComponents = 4;
    command.material.texture.minFilter = SO_TEXTURE_FILTER_NEAREST;
    command.material.texture.magFilter = SO_TEXTURE_FILTER_NEAREST;
    command.material.texture.model = SO_TEXTURE_MODEL_MODULATE;
    command.material.textureAlphaIncludesOpacity = false;
    command.state.blend.enabled = TRUE;
    command.state.blend.srcRGBFactor = SO_BLEND_FACTOR_SRC_ALPHA;
    command.state.blend.dstRGBFactor = SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    command.state.blend.srcAlphaFactor = SO_BLEND_FACTOR_SRC_ALPHA;
    command.state.blend.dstAlphaFactor = SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    if (topology == SO_TOPOLOGY_LINES) {
      command.state.raster.lineWidth = 4.0f;
      command.state.raster.linePattern = 0xFFFD;
    }
    else {
      command.state.raster.pointSize = 512.0f;
    }
    drawlist.addCommand(command);
    return fixture.render(drawlist, SbVec4f(0, 0, 1, 1));
  };

  const std::vector<uint8_t> line = render(
    SO_TOPOLOGY_LINES, linePositions, 2, lineTexcoords);
  const std::vector<uint8_t> point = render(
    SO_TOPOLOGY_POINTS, pointPosition, 1, pointTexcoord);
  const uint8_t * linePixel = pixelAt(line, 32, 32);
  const uint8_t * pointPixel = pixelAt(point, 32, 32);
  const auto hasBlendedRed = [](const uint8_t * pixel) {
    return pixel[0] > 60 && pixel[0] < 200 &&
      pixel[2] > 60 && pixel[2] < 200 && pixel[1] < 40;
  };
  return check(hasBlendedRed(linePixel) && hasBlendedRed(pointPixel),
               "line/point fallback did not preserve texture and alpha semantics");
}

bool testImageNode(Fixture & fixture)
{
  const unsigned char pixels[] = {
    255, 0, 0, 255,   0, 255, 0, 255,
      0, 0, 255, 255, 255, 255, 0, 255
  };
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoImage * image = new SoImage;
  image->image.setValue(SbVec2s(2, 2), 4, pixels);
  image->width = 4;
  image->height = 4;
  root->addChild(image);
  SoIRRenderAction action(SbViewportRegion(64, 64));
  action.apply(root);
  bool retainedSourceAndFootprint = false;
  for (int i = 0; i < action.getDrawList().getNumCommands(); ++i) {
    const SoRenderCommand & command = action.getDrawList().getCommand(i);
    if (command.pixelRaster.enabled) {
      retainedSourceAndFootprint =
        command.material.texture.width == 2 &&
        command.material.texture.height == 2 &&
        command.pixelRaster.width == 4 &&
        command.pixelRaster.height == 4;
      break;
    }
  }
  const std::vector<uint8_t> rendered = fixture.render(
    action.getDrawList(), SbVec4f(0, 0, 1, 1));
  root->unref();
  int red = 0;
  int green = 0;
  int blue = 0;
  int yellow = 0;
  for (int y = 30; y < 38; ++y) {
    for (int x = 30; x < 38; ++x) {
      const uint8_t * pixel = pixelAt(rendered, x, y);
      if (pixel[0] > 220 && pixel[1] < 30 && pixel[2] < 30) ++red;
      if (pixel[0] < 30 && pixel[1] > 220 && pixel[2] < 30) ++green;
      if (pixel[0] < 30 && pixel[1] < 30 && pixel[2] > 220) ++blue;
      if (pixel[0] > 220 && pixel[1] > 220 && pixel[2] < 30) ++yellow;
    }
  }
  return check(retainedSourceAndFootprint && red > 0 && green > 0 &&
               blue > 0 && yellow > 0,
               "SoImage did not preserve source texels while scaling its footprint");
}

bool testTextNode(Fixture & fixture)
{
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoText2 * text = new SoText2;
  text->string = "Coin";
  root->addChild(text);
  const std::vector<uint8_t> rendered = renderNode(
    fixture, root, SbVec4f(0, 0, 0, 1));
  root->unref();
  int nonBlack = 0;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      const uint8_t * pixel = pixelAt(rendered, x, y);
      if (pixel[0] > 30 || pixel[1] > 30 || pixel[2] > 30) ++nonBlack;
    }
  }
  return check(nonBlack > 0,
               "SoText2 did not render through the retained pixel path");
}

bool testPixelDraw(Fixture & fixture)
{
  const float positions[] = {
    -0.1f, -0.1f, 0.0f,  0.1f, -0.1f, 0.0f,
     0.1f,  0.1f, 0.0f, -0.1f,  0.1f, 0.0f
  };
  const float texcoords[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f
  };
  const uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };
  const unsigned char image[] = {
    255, 0, 0, 255, 255, 0, 0, 255,
    255, 0, 0, 255, 255, 0, 0, 255
  };
  SoRenderCommand command;
  command.modelMatrix.makeIdentity();
  command.geometry.topology = SO_TOPOLOGY_TRIANGLES;
  command.geometry.vertexCount = 4;
  command.geometry.indexCount = 6;
  command.geometry.positions = positions;
  command.geometry.indices = indices;
  command.geometry.vertexStride = sizeof(float) * 3;
  command.geometry.texcoords = texcoords;
  command.geometry.texcoordStride = sizeof(float) * 4;
  command.material.texture.pixels = image;
  command.material.texture.width = 2;
  command.material.texture.height = 2;
  command.material.texture.numComponents = 4;
  command.pixelRaster.enabled = TRUE;
  command.pixelRaster.originX = 20;
  command.pixelRaster.originY = 20;
  command.pixelRaster.width = 2;
  command.pixelRaster.height = 2;
  command.material.shadingModel = SO_SHADING_UNLIT;
  SoDrawList drawlist;
  drawlist.addCommand(command);
  const std::vector<uint8_t> pixels = fixture.render(
    drawlist, SbVec4f(0, 0, 1, 1), 1.0f, SbVec2s(8, 8), SbVec2s(48, 48));
  const uint8_t * pixel = pixelAt(pixels, 28, 28);
  return check(pixel[0] > 200 && pixel[1] < 50 && pixel[2] < 50,
               "pixel pipeline did not sample the retained image at its origin");
}

} // namespace

static int runTest()
{
  setEnvironment("COIN_EGL", "1");
  setEnvironment("EGL_PLATFORM", "surfaceless");
  setEnvironment("COIN_EGL_CORE_PROFILE", "1");
  SoDB::init();
  Fixture fixture;
  const int initializationResult = fixture.initialize();
  if (initializationResult != 0) {
    if (initializationResult == 77) {
      return skip("core EGL raster context is unavailable");
    }
    std::cerr << "FAIL: retained raster backend did not initialize on the "
              << "verified OpenGL 3.3/GLSL 330 context" << std::endl;
    return 1;
  }

  int result = 0;
  if (!testWideLine(fixture)) result = 1;
  if (!testPointSize(fixture)) result = 1;
  if (!testFullLinePattern(fixture)) result = 1;
  if (!testLineStripPatternContinuity(fixture)) result = 1;
  if (!testIndexedLinePatternOccurrences(fixture)) result = 1;
  if (!testEmptyLinePattern(fixture)) result = 1;
  if (!testTriangleFallbacks(fixture)) result = 1;
  if (!testPatternedTriangleFallback(fixture)) result = 1;
  if (!testTriangleFallbackCulling(fixture)) result = 1;
  if (!testTriangleStripFallbackCulling(fixture)) result = 1;
  if (!testPolygonOffsetTargets(fixture)) result = 1;
  if (!testSemanticFallback(fixture)) result = 1;
  if (!testPixelDraw(fixture)) result = 1;
  if (!testImageNode(fixture)) result = 1;
  if (!testTextNode(fixture)) result = 1;
  fixture.shutdown();
  return result;
}

int main()
{
  const int result = runTest();
  SoDB::finish();
  return result;
}
