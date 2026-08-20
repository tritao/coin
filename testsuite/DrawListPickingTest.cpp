#include "rendering/SoGLRenderBackend.h"
#include "support/GLTestContext.h"

#include <Inventor/SoDB.h>
#include <Inventor/C/glue/gl.h>
#include <Inventor/system/gl.h>

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
  planner.build(drawlist, SbMatrix::identity(), plan);
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
readPixels(const GLTestContext & context)
{
  return context.readPixels();
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
  SoDB::init();

  GLTestContextConfig config;
  config.profile = GLTestProfile::Core;
  config.major = 3;
  config.minor = 3;
  config.width = 64;
  config.height = 64;
  GLTestContext context;
  if (!context.initialize(config)) {
    return skip("core GLFW OpenGL context is unavailable");
  }

  SoGLRenderBackend backend;
  SoRenderBackendInitParams initparams = {};
  if (!backend.initialize(initparams)) {
    std::cerr << "FAIL: retained picking backend did not initialize on the "
              << "verified OpenGL 3.3/GLSL 330 context" << std::endl;
    return 1;
  }

  const float triangle[] = {
    -0.8f, -0.8f, 0.0f,
     0.8f, -0.8f, 0.0f,
     0.0f,  0.8f, 0.0f
  };
  SoRenderCommand command;
  command.objectId = 0x12345678u;
  command.nodeId = 0x11223344u;
  command.instanceId = 0x55667788u;
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
        hit.nodeId != command.nodeId ||
        hit.instanceId != command.instanceId ||
        hit.objectId != command.objectId ||
        hit.commandIndex != 0 || hit.type != SO_PICK_OBJECT ||
        hit.elementIndex != -1 || !hit.hasDepth) {
      std::cerr << "FAIL: center query did not resolve object ID 1"
                << std::endl;
      result = 1;
    }

    SoPickResult identityOnlyHit;
    if (!backend.pickClosest(32, 32, 0, SoPickReadbackMode::ID_ONLY,
                             identityOnlyHit) ||
        identityOnlyHit.id != hit.id ||
        identityOnlyHit.commandIndex != hit.commandIndex ||
        identityOnlyHit.hasDepth) {
      std::cerr << "FAIL: ID-only synchronous pick changed identity"
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

    SoAsyncPickRequest asyncRequest;
    if (!backend.requestPickClosestAsync(32, 32, 0, asyncRequest)) {
      std::cerr << "FAIL: asynchronous pick request was rejected" << std::endl;
      result = 1;
    }
    else {
      SoPickResult asyncHit;
      const SoAsyncPickStatus initial =
        backend.pollPickClosestAsync(asyncRequest, asyncHit);
      if (initial != SoAsyncPickStatus::PENDING &&
          initial != SoAsyncPickStatus::HIT) {
        std::cerr << "FAIL: asynchronous pick had invalid initial status"
                  << std::endl;
        result = 1;
      }
      if (initial == SoAsyncPickStatus::PENDING) {
        glFinish();
        if (backend.pollPickClosestAsync(asyncRequest, asyncHit) !=
              SoAsyncPickStatus::HIT) {
          std::cerr << "FAIL: asynchronous pick did not become ready"
                    << std::endl;
          result = 1;
        }
      }
      if (asyncHit.id != 1 || asyncHit.commandIndex != 0) {
        std::cerr << "FAIL: asynchronous pick resolved the wrong hit"
                  << std::endl;
        result = 1;
      }
    }

    SoAsyncPickRequest ringRequests[4];
    for (SoAsyncPickRequest & request : ringRequests) {
      if (!backend.requestPickClosestAsync(32, 32, 0, request)) {
        std::cerr << "FAIL: asynchronous pick ring rejected request"
                  << std::endl;
        result = 1;
      }
    }
    SoPickResult superseded;
    if (backend.pollPickClosestAsync(ringRequests[0], superseded) !=
        SoAsyncPickStatus::STALE) {
      std::cerr << "FAIL: overwritten asynchronous request was not stale"
                << std::endl;
      result = 1;
    }
    glFinish();
    if (backend.pollPickClosestAsync(ringRequests[3], superseded) !=
          SoAsyncPickStatus::HIT || superseded.id != 1) {
      std::cerr << "FAIL: newest asynchronous ring request did not resolve"
                << std::endl;
      result = 1;
    }

    SoAsyncPickRequest identityRequest;
    if (!backend.requestPickClosestAsync(32, 32, 0,
          SoPickReadbackMode::ID_ONLY, identityRequest) ||
        identityRequest.mode != SoPickReadbackMode::ID_ONLY) {
      std::cerr << "FAIL: ID-only asynchronous pick was rejected"
                << std::endl;
      result = 1;
    }
    else {
      glFinish();
      SoPickResult identityHit;
      if (backend.pollPickClosestAsync(identityRequest, identityHit) !=
            SoAsyncPickStatus::HIT || identityHit.id != hit.id ||
          identityHit.commandIndex != hit.commandIndex ||
          identityHit.hasDepth) {
        std::cerr << "FAIL: ID-only asynchronous pick changed identity"
                  << std::endl;
        result = 1;
      }
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

    // Visual instancing must not merge interaction identity. These commands
    // share one indexed geometry resource but retain independent pick IDs and
    // selection targets.
    const float instanceQuad[] = {
      -0.35f, -0.35f, 0.0f,  0.35f, -0.35f, 0.0f,
       0.35f,  0.35f, 0.0f, -0.35f,  0.35f, 0.0f
    };
    const uint32_t instanceIndices[] = { 0, 1, 2, 0, 2, 3 };
    SoRenderCommand instanceCommand;
    instanceCommand.geometry.topology = SO_TOPOLOGY_TRIANGLES;
    instanceCommand.geometry.vertexCount = 4;
    instanceCommand.geometry.indexCount = 6;
    instanceCommand.geometry.positions = instanceQuad;
    instanceCommand.geometry.indices = instanceIndices;
    instanceCommand.geometry.vertexStride = sizeof(float) * 3;
    instanceCommand.geometry.resourceKey = 0x5049434b4944ULL;
    instanceCommand.geometry.resourceRevision = 1;
    instanceCommand.material.diffuse = SbVec4f(0.0f, 0.0f, 1.0f, 1.0f);
    instanceCommand.modelMatrix.setTranslate(SbVec3f(-0.5f, 0.0f, 0.0f));
    instanceCommand.objectId = 101;
    instanceCommand.nodeId = 201;
    instanceCommand.instanceId = 301;
    drawlist.clear();
    drawlist.addCommand(instanceCommand);
    instanceCommand.modelMatrix.setTranslate(SbVec3f(0.5f, 0.0f, 0.0f));
    instanceCommand.objectId = 102;
    instanceCommand.nodeId = 202;
    instanceCommand.instanceId = 302;
    drawlist.addCommand(instanceCommand);
    if (!updatePickBuffer(backend, drawlist, params)) {
      std::cerr << "FAIL: instanced-identity pick update failed" << std::endl;
      result = 1;
    }
    else {
      SoPickResult leftHit;
      SoPickResult rightHit;
      if (!backend.pick(16, 32, 0, leftHit) ||
          leftHit.commandIndex != 0 || leftHit.objectId != 101 ||
          leftHit.nodeId != 201 || leftHit.instanceId != 301 ||
          !backend.pick(48, 32, 0, rightHit) ||
          rightHit.commandIndex != 1 || rightHit.objectId != 102 ||
          rightHit.nodeId != 202 || rightHit.instanceId != 302) {
        std::cerr << "FAIL: visual instances lost independent pick identity"
                  << std::endl;
        result = 1;
      }
    }

    SoSelectionState instanceSelection;
    SoSelectionTarget instanceTarget;
    instanceTarget.commandIndex = 1;
    instanceTarget.objectId = 102;
    instanceTarget.nodeId = 202;
    instanceTarget.instanceId = 302;
    instanceTarget.color = SbColor4f(0.0f, 1.0f, 0.0f, 0.75f);
    instanceSelection.selected.push_back(instanceTarget);
    const SoRenderPlan instancePlan = makePlan(drawlist);
    if (!backend.render(drawlist, instancePlan, params, &instanceSelection)) {
      std::cerr << "FAIL: instanced-identity selection render failed"
                << std::endl;
      result = 1;
    }
    else {
      glFinish();
      const std::vector<uint8_t> pixels = readPixels(context);
      const uint8_t * left = pixelAt(pixels, 16, 32);
      const uint8_t * right = pixelAt(pixels, 48, 32);
      if (left[2] < 200 || left[1] > 30 ||
          right[1] < 150 || right[2] > 100) {
        std::cerr << "FAIL: selection did not address one visual instance"
                  << std::endl;
        result = 1;
      }
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

      SoPickResultList depthStack;
      if (!backend.pickDepthStack(32, 32, 0, 8, 32, depthStack) ||
          depthStack.generation != drawlist.getGeneration() ||
          depthStack.hits.size() != 2 || depthStack.hits[0].id != 1 ||
          depthStack.hits[1].id != 2 ||
          !(depthStack.hits[0].depth < depthStack.hits[1].depth) ||
          depthStack.truncated) {
        std::cerr << "FAIL: bounded depth peeling did not return both "
                     "transparent layers front-to-back" << std::endl;
        result = 1;
      }

      SoPickResultList limitedStack;
      if (!backend.pickDepthStack(32, 32, 0, 1, 32, limitedStack) ||
          limitedStack.hits.size() != 1 || !limitedStack.truncated) {
        std::cerr << "FAIL: depth-layer limit was not reported"
                  << std::endl;
        result = 1;
      }

      SoPickResultList visible;
      if (!backend.pickVisibleRegion(SbBox2s(30, 30, 34, 34), visible) ||
          visible.hits.size() != 1 || visible.hits[0].id != 1) {
        std::cerr << "FAIL: visible-region picking did not deduplicate IDs"
                  << std::endl;
        result = 1;
      }

      SoRenderCommand mainNear = command;
      mainNear.modelMatrix.setTranslate(SbVec3f(0.0f, 0.0f, -0.4f));
      SoRenderCommand mainFar = command;
      mainFar.modelMatrix.setTranslate(SbVec3f(0.0f, 0.0f, 0.4f));
      SoRenderCommand localNear = command;
      localNear.modelMatrix.setTranslate(SbVec3f(0.0f, 0.0f, 0.2f));
      SoRenderCommand localFar = command;
      localFar.modelMatrix.setTranslate(SbVec3f(0.0f, 0.0f, 0.6f));
      drawlist.clear();
      drawlist.addCommand(mainNear);
      drawlist.addCommand(mainFar);
      SoDepthClearEvent localClear;
      localClear.sequence = 2;
      localClear.viewportOverride = TRUE;
      localClear.viewportX = 32;
      localClear.viewportY = 0;
      localClear.viewportWidth = 32;
      localClear.viewportHeight = 64;
      drawlist.addDepthClearEvent(localClear);
      drawlist.addCommand(localNear);
      drawlist.addCommand(localFar);
      if (!updatePickBuffer(backend, drawlist, params)) {
        std::cerr << "FAIL: viewport-local depth-segment update failed"
                  << std::endl;
        result = 1;
      }
      else {
        SoPickResultList insideSegments;
        if (!backend.pickDepthStack(48, 20, 0, 8, 32, insideSegments) ||
            insideSegments.hits.size() != 4 ||
            insideSegments.hits[0].id != 3 ||
            insideSegments.hits[1].id != 4 ||
            insideSegments.hits[2].id != 1 ||
            insideSegments.hits[3].id != 2) {
          std::cerr << "FAIL: local depth-clear candidates did not follow "
                       "visual segment order:";
          for (const SoPickResult & hit : insideSegments.hits) {
            std::cerr << ' ' << hit.id;
          }
          std::cerr << std::endl;
          result = 1;
        }

        SoPickResultList outsideSegments;
        if (!backend.pickDepthStack(16, 20, 0, 8, 32, outsideSegments) ||
            outsideSegments.hits.size() != 4 ||
            outsideSegments.hits[0].id != 1 ||
            outsideSegments.hits[1].id != 3 ||
            outsideSegments.hits[2].id != 2 ||
            outsideSegments.hits[3].id != 4) {
          std::cerr << "FAIL: local depth clear affected picking outside its "
                       "viewport:";
          for (const SoPickResult & hit : outsideSegments.hits) {
            std::cerr << ' ' << hit.id;
          }
          std::cerr << std::endl;
          result = 1;
        }
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
    // The state-preservation probe deliberately restores the default
    // framebuffer. Rebind the shared test FBO before continuing with the
    // visual selection checks.
    context.bindFramebuffer();

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
      const SoRenderStatistics faceStatistics =
        backend.getRenderStatistics();
      if (faceStatistics.pickDrawCalls != 1 ||
          faceStatistics.pickInstancedEntries != 1) {
        std::cerr << "FAIL: contiguous face mapping did not use primitive ID"
                  << std::endl;
        result = 1;
      }
    }

    // A partial mapping cannot derive its lookup ID from gl_PrimitiveID and
    // must retain the explicit range draw.
    const uint32_t partialIndices[] = { 0, 1, 2, 0, 1, 2 };
    SoRenderCommand partialRangeCommand = faceCommand;
    partialRangeCommand.geometry.indices = partialIndices;
    partialRangeCommand.geometry.indexCount = 6;
    partialRangeCommand.pick.elementRanges[0].drawStart = 3;
    drawlist.clear();
    drawlist.addCommand(partialRangeCommand);
    if (!updatePickBuffer(backend, drawlist, params)) {
      std::cerr << "FAIL: partial-range pick buffer update failed" << std::endl;
      result = 1;
    }
    else {
      SoPickResult partialHit;
      const SoRenderStatistics partialStatistics =
        backend.getRenderStatistics();
      if (!backend.pick(32, 32, 0, partialHit) ||
          partialHit.elementIndex != 2 ||
          partialStatistics.pickDrawCalls != 1 ||
          partialStatistics.pickInstancedEntries != 0) {
        std::cerr << "FAIL: partial face mapping did not use range fallback"
                  << std::endl;
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
        const std::vector<uint8_t> pixels = readPixels(context);
        const uint8_t * center = pixelAt(pixels, 32, 32);
        if (center[0] < 70 || center[2] < 40) {
          std::cerr << "FAIL: selection overlay did not blend over the object"
                    << std::endl;
          result = 1;
        }
      }
    }

    // Compatible whole-command overlays share one instance draw while
    // retaining a separate color and transform for each target.
    SoRenderCommand leftSelectionCommand = command;
    leftSelectionCommand.geometry.resourceKey = 0x53454c454354ULL;
    leftSelectionCommand.modelMatrix.setTranslate(SbVec3f(-0.2f, 0.0f, 0.0f));
    SoRenderCommand rightSelectionCommand = command;
    rightSelectionCommand.geometry.resourceKey = 0x53454c454354ULL;
    rightSelectionCommand.modelMatrix.setTranslate(SbVec3f(0.2f, 0.0f, 0.0f));
    drawlist.clear();
    drawlist.addCommand(leftSelectionCommand);
    drawlist.addCommand(rightSelectionCommand);
    SoSelectionState instancedSelection;
    SoSelectionTarget leftTarget;
    leftTarget.commandIndex = 0;
    leftTarget.color = SbColor4f(1.0f, 0.0f, 0.0f, 0.5f);
    instancedSelection.selected.push_back(leftTarget);
    SoSelectionTarget rightTarget;
    rightTarget.commandIndex = 1;
    rightTarget.color = SbColor4f(0.0f, 1.0f, 0.0f, 0.5f);
    instancedSelection.selected.push_back(rightTarget);
    if (!render(backend, drawlist, params) ||
        !backend.renderSelection(drawlist, instancedSelection, params)) {
      std::cerr << "FAIL: instanced selection overlay failed" << std::endl;
      result = 1;
    }
    else {
      const SoRenderStatistics selectionStatistics =
        backend.getRenderStatistics();
      if (selectionStatistics.selectionOverlayDrawCalls != 1 ||
          selectionStatistics.selectionInstancedBatches != 1 ||
          selectionStatistics.selectionInstancedEntries != 2 ||
          selectionStatistics.selectedOverlayEntries != 2) {
        std::cerr << "FAIL: compatible selection targets did not batch"
                  << std::endl;
        result = 1;
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
      const std::vector<uint8_t> pixels = readPixels(context);
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
      const std::vector<uint8_t> pixels = readPixels(context);
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
      const std::vector<uint8_t> pixels = readPixels(context);
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
  return result;
}

int
main()
{
  const int result = runTest();
  SoDB::finish();
  return result;
}
