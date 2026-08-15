#include "rendering/CoinOffscreenGLCanvas.h"
#include "rendering/SoGLRenderBackend.h"

#include <Inventor/SoDB.h>
#include <Inventor/C/glue/gl.h>
#include <Inventor/system/gl.h>

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

int
skip(const char * reason)
{
  std::cout << "SKIP: " << reason << std::endl;
  return 77;
}

void
setEnvironment(const char * name, const char * value)
{
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

SoRenderParams
renderParams()
{
  SoRenderParams params = {};
  params.viewport = SbViewportRegion(64, 64);
  params.viewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(64, 64));
  params.viewMatrix.makeIdentity();
  params.projMatrix.makeIdentity();
  params.clearColor = SbColor4f(0.0f, 0.0f, 0.0f, 1.0f);
  params.clearDepth = 1.0f;
  params.flags = SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH;
  return params;
}

SoRenderPlan
makePlan(const SoDrawList & drawlist)
{
  SoRenderPlan plan;
  SoRenderPlanner planner;
  planner.build(drawlist, plan);
  return plan;
}

SbBool
render(SoGLRenderBackend & backend, const SoDrawList & drawlist,
       const SoRenderParams & params)
{
  const SoRenderPlan plan = makePlan(drawlist);
  return backend.render(drawlist, plan, params);
}

SbBool
updatePickBuffer(SoGLRenderBackend & backend, const SoDrawList & drawlist,
                 const SoRenderParams & params)
{
  const SoRenderPlan plan = makePlan(drawlist);
  return backend.updatePickBuffer(drawlist, plan, params);
}

std::vector<uint8_t>
readPixels(const CoinOffscreenGLCanvas & canvas)
{
  std::vector<uint8_t> pixels(64 * 64 * 4, 0);
  canvas.readPixels(pixels.data(), SbVec2s(64, 64), 64, 4);
  return pixels;
}

const uint8_t *
pixelAt(const std::vector<uint8_t> & pixels, const int x, const int y)
{
  return &pixels[static_cast<size_t>(y * 64 + x) * 4];
}

} // namespace

static int
runTest()
{
  setEnvironment("COIN_EGL", "1");
  setEnvironment("EGL_PLATFORM", "surfaceless");
  setEnvironment("COIN_EGL_CORE_PROFILE", "1");
#if defined(__linux__)
#endif
  SoDB::init();

  CoinOffscreenGLCanvas canvas;
  canvas.setWantedSize(SbVec2s(64, 64));
  if (canvas.activateGLContext() == 0) {
    return skip("core EGL offscreen context is unavailable");
  }

  SoGLRenderBackend backend;
  SoRenderBackendInitParams initparams = {};
  if (!backend.initialize(initparams)) {
    std::cerr << "FAIL: retained picking backend did not initialize on the "
              << "verified OpenGL 3.3/GLSL 330 context" << std::endl;
    canvas.deactivateGLContext();
    return 1;
  }

  const float triangle[] = {
    -0.8f, -0.8f, 0.0f,
     0.8f, -0.8f, 0.0f,
     0.0f,  0.8f, 0.0f
  };
  SoRenderCommand command;
  command.objectId = 0x12345678u;
  command.modelMatrix.makeIdentity();
  command.geometry.topology = SO_TOPOLOGY_TRIANGLES;
  command.geometry.vertexCount = 3;
  command.geometry.positions = triangle;
  command.geometry.vertexStride = sizeof(float) * 3;
  command.material.shadingModel = SO_SHADING_UNLIT;
  command.material.diffuse = SbVec4f(0.0f, 0.0f, 1.0f, 1.0f);

  SoDrawList drawlist;
  drawlist.addCommand(command);
  const SoRenderParams params = renderParams();
  int result = 0;

  // Updating the integer ID target is explicit. Ordinary visual rendering
  // does not populate it as a side effect.
  if (!updatePickBuffer(backend, drawlist, params)) {
    std::cerr << "FAIL: integer pick buffer could not be updated" << std::endl;
    result = 1;
  }
  else {
    SoPickResult hit;
    if (!backend.pick(32, 32, 0, hit) || hit.id != 1 ||
        hit.objectId != command.objectId ||
        hit.commandIndex != 0 || hit.type != SO_PICK_OBJECT ||
        hit.elementIndex != -1) {
      std::cerr << "FAIL: center query did not resolve object ID 1"
                << std::endl;
      result = 1;
    }

    SoPickResult miss;
    if (backend.pick(1, 1, 0, miss)) {
      std::cerr << "FAIL: background query returned a pick" << std::endl;
      result = 1;
    }

    SoPickResult radiusHit;
    if (!backend.pick(30, 32, 2, radiusHit) || radiusHit.id != 1) {
      std::cerr << "FAIL: radius query did not resolve nearby geometry"
                << std::endl;
      result = 1;
    }

    // The ID buffer owns a snapshot of the frame-local LUT.  A query remains
    // valid after the source DrawList has been destroyed.
    {
      SoDrawList temporary;
      temporary.addCommand(command);
      if (!updatePickBuffer(backend, temporary, params)) {
        std::cerr << "FAIL: snapshot pick buffer update failed" << std::endl;
        result = 1;
      }
    }
    SoPickResult snapshotHit;
    if (!backend.pick(32, 32, 0, snapshotHit) || snapshotHit.id != 1 ||
        snapshotHit.commandIndex != 0) {
      std::cerr << "FAIL: pick result depended on DrawList lifetime"
                << std::endl;
      result = 1;
    }

    // Explicit picking writes depth even for commands classified as
    // transparent.  The nearest command must win independent of draw order.
    SoRenderCommand nearCommand = command;
    nearCommand.opacityClass = SO_OPACITY_TRANSPARENT;
    nearCommand.state.depth.func = SO_DEPTH_LESS;
    nearCommand.state.depth.writeEnabled = FALSE;
    nearCommand.modelMatrix.setTranslate(SbVec3f(0.0f, 0.0f, -0.25f));
    SoRenderCommand farCommand = command;
    farCommand.opacityClass = SO_OPACITY_TRANSPARENT;
    farCommand.state.depth.func = SO_DEPTH_LESS;
    farCommand.state.depth.writeEnabled = FALSE;
    farCommand.modelMatrix.setTranslate(SbVec3f(0.0f, 0.0f, 0.25f));
    drawlist.clear();
    drawlist.addCommand(farCommand);
    drawlist.addCommand(nearCommand);
    if (!updatePickBuffer(backend, drawlist, params)) {
      std::cerr << "FAIL: transparent pick buffer update failed" << std::endl;
      result = 1;
    }
    else {
      SoPickResult nearHit;
      if (!backend.pick(32, 32, 0, nearHit) || nearHit.id != 2) {
        std::cerr << "FAIL: transparent near command did not win"
                  << std::endl;
        result = 1;
      }
    }
    drawlist.clear();
    drawlist.addCommand(nearCommand);
    drawlist.addCommand(farCommand);
    if (!updatePickBuffer(backend, drawlist, params)) {
      std::cerr << "FAIL: reversed transparent pick update failed" << std::endl;
      result = 1;
    }
    else {
      SoPickResult reversedNearHit;
      if (!backend.pick(32, 32, 0, reversedNearHit) ||
          reversedNearHit.id != 1) {
        std::cerr << "FAIL: transparent depth result depended on draw order"
                  << std::endl;
        result = 1;
      }
    }

    // All explicit operations preserve the caller's mutable GL state,
    // including front-face selection, separate framebuffer bindings,
    // pixel-pack state and a bound pixel-pack buffer.
    drawlist.clear();
    drawlist.addCommand(command);
    while (glGetError() != GL_NO_ERROR) {}

    GLuint stateDrawFramebuffer = 0;
    GLuint stateReadFramebuffer = 0;
    GLuint stateColorTexture = 0;
    GLuint stateDepthBuffer = 0;
    const cc_glglue * glue = context.glue();
    cc_glglue_glGenFramebuffers(glue, 1, &stateDrawFramebuffer);
    cc_glglue_glGenFramebuffers(glue, 1, &stateReadFramebuffer);
    glGenTextures(1, &stateColorTexture);
    glBindTexture(GL_TEXTURE_2D, stateColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    cc_glglue_glBindFramebuffer(glue, GL_DRAW_FRAMEBUFFER, stateDrawFramebuffer);
    cc_glglue_glFramebufferTexture2D(glue, GL_DRAW_FRAMEBUFFER,
                                     GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                     stateColorTexture, 0);
    cc_glglue_glGenRenderbuffers(glue, 1, &stateDepthBuffer);
    cc_glglue_glBindRenderbuffer(glue, GL_RENDERBUFFER, stateDepthBuffer);
    cc_glglue_glRenderbufferStorage(glue, GL_RENDERBUFFER,
                                    GL_DEPTH_COMPONENT24, 64, 64);
    cc_glglue_glFramebufferRenderbuffer(glue, GL_DRAW_FRAMEBUFFER,
                                        GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                        stateDepthBuffer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    cc_glglue_glBindFramebuffer(glue, GL_READ_FRAMEBUFFER, stateReadFramebuffer);
    glReadBuffer(GL_NONE);

    glViewport(3, 4, 20, 21);
    glFrontFace(GL_CW);
    glEnable(GL_BLEND);
    cc_glglue_glBlendEquationSeparate(glue, GL_MAX, GL_MIN);
    glEnable(GL_SCISSOR_TEST);
    glScissor(2, 3, 11, 12);
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glPixelStorei(GL_PACK_ROW_LENGTH, 17);
    glPixelStorei(GL_PACK_SKIP_ROWS, 3);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 5);
    glPixelStorei(GL_PACK_SWAP_BYTES, GL_TRUE);
    glPixelStorei(GL_PACK_LSB_FIRST, GL_TRUE);
    glPixelStorei(GL_PACK_IMAGE_HEIGHT, 19);
    glPixelStorei(GL_PACK_SKIP_IMAGES, 2);
    GLuint pixelPackBuffer = 0;
    cc_glglue_glGenBuffers(glue, 1, &pixelPackBuffer);
    cc_glglue_glBindBuffer(glue, GL_PIXEL_PACK_BUFFER, pixelPackBuffer);
    cc_glglue_glBufferData(glue, GL_PIXEL_PACK_BUFFER,
                           64 * 64 * sizeof(GLuint), nullptr, GL_STREAM_READ);
    if (!updatePickBuffer(backend, drawlist, params)) {
      std::cerr << "FAIL: state-preservation pick update failed" << std::endl;
      result = 1;
    }
    SoPickResult preservedStateHit;
    const bool preservedStatePick =
      backend.pick(32, 32, 0, preservedStateHit);
    const bool preservedStateSelection =
      backend.renderSelection(drawlist, SoSelectionState(), params);
    if (!preservedStatePick || !preservedStateSelection) {
      std::cerr << "FAIL: state-preservation operation failed (pick="
                << preservedStatePick << ", selection="
                << preservedStateSelection << ")"
                << std::endl;
      result = 1;
    }
    GLint preservedViewport[4] = { 0, 0, 0, 0 };
    GLint preservedFrontFace = GL_CCW;
    GLint preservedDrawFramebuffer = 0;
    GLint preservedReadFramebuffer = 0;
    GLint preservedPixelPackBuffer = 0;
    GLint preservedPackAlignment = 0;
    GLint preservedPackRowLength = 0;
    GLint preservedPackSkipRows = 0;
    GLint preservedPackSkipPixels = 0;
    GLint preservedPackSwapBytes = 0;
    GLint preservedPackLSBFirst = 0;
    GLint preservedPackImageHeight = 0;
    GLint preservedPackSkipImages = 0;
    GLint preservedBlendEquationRGB = GL_FUNC_ADD;
    GLint preservedBlendEquationAlpha = GL_FUNC_ADD;
    GLboolean preservedColorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    GLboolean preservedDepthWrite = GL_TRUE;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &preservedDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &preservedReadFramebuffer);
    glGetIntegerv(GL_VIEWPORT, preservedViewport);
    glGetIntegerv(GL_FRONT_FACE, &preservedFrontFace);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &preservedPixelPackBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &preservedPackAlignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &preservedPackRowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &preservedPackSkipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &preservedPackSkipPixels);
    glGetIntegerv(GL_PACK_SWAP_BYTES, &preservedPackSwapBytes);
    glGetIntegerv(GL_PACK_LSB_FIRST, &preservedPackLSBFirst);
    glGetIntegerv(GL_PACK_IMAGE_HEIGHT, &preservedPackImageHeight);
    glGetIntegerv(GL_PACK_SKIP_IMAGES, &preservedPackSkipImages);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &preservedBlendEquationRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &preservedBlendEquationAlpha);
    glGetBooleanv(GL_COLOR_WRITEMASK, preservedColorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &preservedDepthWrite);
    if (preservedViewport[0] != 3 || preservedViewport[1] != 4 ||
        preservedViewport[2] != 20 || preservedViewport[3] != 21 ||
        preservedFrontFace != GL_CW ||
        preservedDrawFramebuffer != static_cast<GLint>(stateDrawFramebuffer) ||
        preservedReadFramebuffer != static_cast<GLint>(stateReadFramebuffer) ||
        preservedPixelPackBuffer != static_cast<GLint>(pixelPackBuffer) ||
        preservedPackAlignment != 8 || preservedPackRowLength != 17 ||
        preservedPackSkipRows != 3 || preservedPackSkipPixels != 5 ||
        preservedPackSwapBytes != GL_TRUE || preservedPackLSBFirst != GL_TRUE ||
        preservedPackImageHeight != 19 || preservedPackSkipImages != 2 ||
        preservedBlendEquationRGB != GL_MAX ||
        preservedBlendEquationAlpha != GL_MIN ||
        preservedColorMask[0] != GL_FALSE ||
        preservedColorMask[1] != GL_TRUE ||
        preservedColorMask[2] != GL_FALSE ||
        preservedColorMask[3] != GL_TRUE ||
        preservedDepthWrite != GL_FALSE) {
      std::cerr << "FAIL: explicit picking/selection leaked GL state"
                << std::endl;
      result = 1;
    }
    cc_glglue_glBindBuffer(glue, GL_PIXEL_PACK_BUFFER, 0);
    cc_glglue_glDeleteBuffers(glue, 1, &pixelPackBuffer);
    cc_glglue_glBindFramebuffer(glue, GL_FRAMEBUFFER, 0);
    cc_glglue_glDeleteRenderbuffers(glue, 1, &stateDepthBuffer);
    glDeleteTextures(1, &stateColorTexture);
    cc_glglue_glDeleteFramebuffers(glue, 1, &stateReadFramebuffer);
    cc_glglue_glDeleteFramebuffers(glue, 1, &stateDrawFramebuffer);
    glViewport(0, 0, 64, 64);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);

    // Explicit non-indexed ranges resolve to the retained subelement rather
    // than requiring a per-vertex encoded identity.
    const uint32_t faceIndices[] = { 0, 1, 2 };
    SoRenderCommand faceCommand = command;
    faceCommand.geometry.indices = faceIndices;
    faceCommand.geometry.indexCount = 3;
    faceCommand.state.depth.range[0] = 0.25f;
    faceCommand.state.depth.range[1] = 0.75f;
    SoRenderElementRange faceRange;
    faceRange.type = SO_PICK_FACE;
    faceRange.elementIndex = 2;
    faceRange.drawStart = 0;
    faceRange.drawCount = 3;
    faceCommand.pick.elementRanges.push_back(faceRange);
    drawlist.clear();
    drawlist.addCommand(faceCommand);
    if (!updatePickBuffer(backend, drawlist, params)) {
      std::cerr << "FAIL: indexed subelement pick buffer update failed"
                << std::endl;
      result = 1;
    }
    else {
      SoPickResult faceHit;
      if (!backend.pick(32, 32, 0, faceHit) || faceHit.id != 1 ||
          faceHit.type != SO_PICK_FACE || faceHit.elementIndex != 2) {
        std::cerr << "FAIL: indexed face range did not resolve" << std::endl;
        result = 1;
      }
    }

    // An explicitly empty or out-of-bounds range is invalid; it must not
    // silently turn into a whole-command object entry.
    SoRenderCommand invalidRangeCommand = command;
    SoRenderElementRange invalidRange;
    invalidRange.type = SO_PICK_FACE;
    invalidRange.elementIndex = 4;
    invalidRange.drawStart = 0;
    invalidRange.drawCount = 0;
    invalidRangeCommand.pick.elementRanges.push_back(invalidRange);
    drawlist.clear();
    drawlist.addCommand(invalidRangeCommand);
    if (!updatePickBuffer(backend, drawlist, params)) {
      std::cerr << "FAIL: invalid-range pick buffer update failed" << std::endl;
      result = 1;
    }
    else {
      SoPickResult invalidRangeHit;
      if (backend.pick(32, 32, 0, invalidRangeHit)) {
        std::cerr << "FAIL: invalid range generated a whole-command pick"
                  << std::endl;
        result = 1;
      }
    }

    // Selection is an explicit overlay operation.  It reuses the retained
    // command coverage while changing only the output color and does not
    // populate the integer pick target implicitly.
    drawlist.clear();
    drawlist.addCommand(command);
    if (!render(backend, drawlist, params)) {
      std::cerr << "FAIL: visual render before selection failed" << std::endl;
      result = 1;
    }
    else {
      SoSelectionState selection;
      SoSelectionTarget target;
      target.commandIndex = 0;
      target.type = SO_PICK_OBJECT;
      target.elementIndex = -1;
      target.color = SbColor4f(1.0f, 0.0f, 0.0f, 0.5f);
      selection.selected.push_back(target);
      if (!backend.renderSelection(drawlist, selection, params)) {
        std::cerr << "FAIL: selection overlay failed" << std::endl;
        result = 1;
      }
      else {
        glFinish();
        const std::vector<uint8_t> pixels = readPixels(canvas);
        const uint8_t * center = pixelAt(pixels, 32, 32);
        if (center[0] < 70 || center[2] < 40) {
          std::cerr << "FAIL: selection overlay did not blend over the object"
                    << std::endl;
          result = 1;
        }
      }
    }

    // Explicit highlighting is independent of hit-test eligibility.
    SoRenderCommand nonPickableCommand = command;
    nonPickableCommand.pick.pickable = false;
    drawlist.clear();
    drawlist.addCommand(nonPickableCommand);
    SoSelectionState nonPickableSelection;
    SoSelectionTarget nonPickableTarget;
    nonPickableTarget.commandIndex = 0;
    nonPickableTarget.color = SbColor4f(1.0f, 0.0f, 0.0f, 0.5f);
    nonPickableSelection.selected.push_back(nonPickableTarget);
    if (!render(backend, drawlist, params) ||
        !backend.renderSelection(drawlist, nonPickableSelection, params)) {
      std::cerr << "FAIL: non-pickable selection overlay failed" << std::endl;
      result = 1;
    }
    else {
      glFinish();
      const std::vector<uint8_t> pixels = readPixels(canvas);
      const uint8_t * center = pixelAt(pixels, 32, 32);
      if (center[0] < 70 || center[2] < 40) {
        std::cerr << "FAIL: non-pickable command could not be selected"
                  << std::endl;
        result = 1;
      }
    }

    // A selection target can address the same retained range as an integer
    // pick without encoding identity in vertex attributes.
    drawlist.clear();
    drawlist.addCommand(faceCommand);
    SoSelectionState faceSelection;
    SoSelectionTarget faceTarget;
    faceTarget.commandIndex = 0;
    faceTarget.type = SO_PICK_FACE;
    faceTarget.elementIndex = 2;
    faceTarget.color = SbColor4f(0.0f, 1.0f, 0.0f, 0.5f);
    faceSelection.selected.push_back(faceTarget);
    if (!render(backend, drawlist, params) ||
        !backend.renderSelection(drawlist, faceSelection, params)) {
      std::cerr << "FAIL: subelement selection overlay failed" << std::endl;
      result = 1;
    }
    else {
      glFinish();
      const std::vector<uint8_t> pixels = readPixels(canvas);
      const uint8_t * center = pixelAt(pixels, 32, 32);
      if (center[1] < 70 || center[2] < 40) {
        std::cerr << "FAIL: subelement selection did not use its draw range"
                  << std::endl;
        result = 1;
      }
    }

    // Triangle wireframe fallback must preserve both edge coverage and the
    // line pattern; it cannot rely on native polygon mode when the width is
    // outside the driver's range.
    SoRenderCommand wireTriangle = command;
    wireTriangle.state.raster.fillMode = SO_RASTER_LINES;
    wireTriangle.state.raster.lineWidth = 8.0f;
    wireTriangle.state.raster.linePattern = 0x0001;
    drawlist.clear();
    drawlist.addCommand(wireTriangle);
    if (!updatePickBuffer(backend, drawlist, params)) {
      std::cerr << "FAIL: triangle wireframe pick buffer update failed"
                << std::endl;
      result = 1;
    }
    else {
      SoPickResult wireHit;
      if (!backend.pick(8, 7, 4, wireHit) || wireHit.id != 1) {
        std::cerr << "FAIL: patterned triangle wireframe was not pickable"
                  << std::endl;
        result = 1;
      }
    }

    // Wide lines and large points use the same retained geometry coverage in the
    // ID pass. The exact native limit is intentionally exceeded here.
    const float linePositions[] = {
      -0.8f, 0.0f, 0.0f, 0.8f, 0.0f, 0.0f
    };
    SoRenderCommand lineCommand;
    lineCommand.modelMatrix.makeIdentity();
    lineCommand.geometry.topology = SO_TOPOLOGY_LINES;
    lineCommand.geometry.vertexCount = 2;
    lineCommand.geometry.positions = linePositions;
    lineCommand.geometry.vertexStride = sizeof(float) * 3;
    lineCommand.state.raster.lineWidth = 2.0f;
    lineCommand.state.raster.linePattern = 0x0001;
    drawlist.clear();
    drawlist.addCommand(lineCommand);
    if (!updatePickBuffer(backend, drawlist, params)) {
      std::cerr << "FAIL: wide-line pick buffer update failed" << std::endl;
      result = 1;
    }
    else {
      SoPickResult lineHit;
      if (!backend.pick(8, 32, 4, lineHit) || lineHit.id != 1) {
        std::cerr << "FAIL: wide-line fallback was not pickable" << std::endl;
        result = 1;
      }
    }

    const float pointPosition[] = { 0.0f, 0.0f, 0.0f };
    SoRenderCommand pointCommand;
    pointCommand.modelMatrix.makeIdentity();
    pointCommand.geometry.topology = SO_TOPOLOGY_POINTS;
    pointCommand.geometry.vertexCount = 1;
    pointCommand.geometry.positions = pointPosition;
    pointCommand.geometry.vertexStride = sizeof(float) * 3;
    pointCommand.state.raster.pointSize = 512.0f;
    drawlist.clear();
    drawlist.addCommand(pointCommand);
    if (!updatePickBuffer(backend, drawlist, params)) {
      std::cerr << "FAIL: large-point pick buffer update failed" << std::endl;
      result = 1;
    }
    else {
      SoPickResult pointHit;
      if (!backend.pick(32, 32, 0, pointHit) || pointHit.id != 1) {
        std::cerr << "FAIL: large-point fallback was not pickable" << std::endl;
        result = 1;
      }
    }

    const float pixelPositions[] = {
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
    };
    const float pixelTexcoords[] = {
      0.0f, 0.0f, 0.0f, 0.0f,
      2.0f, 0.0f, 0.0f, 0.0f,
      2.0f, 2.0f, 0.0f, 0.0f,
      0.0f, 2.0f, 0.0f, 0.0f
    };
    const uint32_t pixelIndices[] = { 0, 1, 2, 0, 2, 3 };
    const unsigned char pixelData[] = {
      255, 255, 255, 255, 255, 255, 255, 255,
      255, 255, 255, 255, 255, 255, 255, 255
    };
    SoRenderCommand pixelCommand;
    pixelCommand.modelMatrix.makeIdentity();
    pixelCommand.geometry.topology = SO_TOPOLOGY_TRIANGLES;
    pixelCommand.geometry.vertexCount = 4;
    pixelCommand.geometry.indexCount = 6;
    pixelCommand.geometry.positions = pixelPositions;
    pixelCommand.geometry.indices = pixelIndices;
    pixelCommand.geometry.vertexStride = sizeof(float) * 3;
    pixelCommand.geometry.texcoords = pixelTexcoords;
    pixelCommand.geometry.texcoordStride = sizeof(float) * 4;
    pixelCommand.material.texture.pixels = pixelData;
    pixelCommand.material.texture.width = 2;
    pixelCommand.material.texture.height = 2;
    pixelCommand.material.texture.numComponents = 4;
    pixelCommand.pixelRaster.enabled = TRUE;
    pixelCommand.pixelRaster.originX = 20;
    pixelCommand.pixelRaster.originY = 20;
    pixelCommand.pixelRaster.width = 2;
    pixelCommand.pixelRaster.height = 2;
    drawlist.clear();
    drawlist.addCommand(pixelCommand);
    if (!updatePickBuffer(backend, drawlist, params)) {
      std::cerr << "FAIL: pixel pick buffer update failed" << std::endl;
      result = 1;
    }
    else {
      SoPickResult pixelHit;
      if (!backend.pick(21, 21, 4, pixelHit) || pixelHit.id != 1) {
        std::cerr << "FAIL: pixel raster command was not pickable" << std::endl;
        result = 1;
      }
    }

    // Pixel producers provide final RGBA.  A low material alpha must not be
    // multiplied into an opaque texel a second time by picking or selection.
    SoRenderCommand pixelCoverageCommand = pixelCommand;
    pixelCoverageCommand.material.diffuse[3] = 0.1f;
    pixelCoverageCommand.state.alphaTest.policy = SO_ALPHA_TEST_POLICY_EXPLICIT;
    pixelCoverageCommand.state.alphaTest.function = SO_ALPHA_TEST_GREATER;
    pixelCoverageCommand.state.alphaTest.reference = 0.5f;
    drawlist.clear();
    drawlist.addCommand(pixelCoverageCommand);
    if (!render(backend, drawlist, params) ||
        !updatePickBuffer(backend, drawlist, params)) {
      std::cerr << "FAIL: pixel coverage parity setup failed" << std::endl;
      result = 1;
    }
    else {
      const std::vector<uint8_t> pixels = readPixels(canvas);
      const uint8_t * pixel = pixelAt(pixels, 21, 21);
      SoPickResult coverageHit;
      if (pixel[0] == 0 || !backend.pick(21, 21, 0, coverageHit)) {
        std::cerr << "FAIL: opaque pixel coverage was changed by material alpha"
                  << std::endl;
        result = 1;
      }
      SoSelectionState coverageSelection;
      SoSelectionTarget coverageTarget;
      coverageTarget.commandIndex = 0;
      coverageTarget.color = SbColor4f(1.0f, 0.0f, 0.0f, 0.5f);
      coverageSelection.selected.push_back(coverageTarget);
      if (!backend.renderSelection(drawlist, coverageSelection, params)) {
        std::cerr << "FAIL: pixel coverage selection failed" << std::endl;
        result = 1;
      }
    }
  }

  backend.shutdown();
  canvas.deactivateGLContext();
  return result;
}

int
main()
{
  const int result = runTest();
  SoDB::finish();
  return result;
}
