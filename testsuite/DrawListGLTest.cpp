#include "rendering/SoGLRenderBackend.h"
#include "rendering/SoRenderPlan.h"
#include "support/GLTestContext.h"

#include <Inventor/SoDB.h>
#include <Inventor/system/gl.h>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

namespace {

int skip(const char * reason)
{
  std::cout << "SKIP: " << reason << std::endl;
  return 77;
}

SoRenderParams renderParams()
{
  SoRenderParams params;
  params.viewport = SbViewportRegion(32, 32);
  params.viewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(32, 32));
  params.viewMatrix.makeIdentity();
  params.projMatrix.makeIdentity();
  params.clearColor.setValue(0.0f, 0.0f, 0.0f, 1.0f);
  params.clearDepth = 1.0f;
  params.flags = SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH;
  return params;
}

SbBool renderWithPlan(SoGLRenderBackend & backend,
                      const SoDrawList & drawlist,
                      const SoRenderParams & params)
{
  SoRenderPlanner planner;
  SoRenderPlan plan;
  planner.build(drawlist, params.viewMatrix, plan);
  return backend.render(drawlist, plan, params);
}

std::vector<uint8_t> readPixels(const GLTestContext & context)
{
  return context.readPixels();
}

const uint8_t * pixelAt(const std::vector<uint8_t> & pixels, int x, int y)
{
  return &pixels[static_cast<size_t>(y * 32 + x) * 4];
}

bool nearColor(const uint8_t * pixel, int red, int green, int blue)
{
  return std::abs(static_cast<int>(pixel[0]) - red) < 35 &&
    std::abs(static_cast<int>(pixel[1]) - green) < 35 &&
    std::abs(static_cast<int>(pixel[2]) - blue) < 35;
}

SoRenderCommand texturedQuad(const float * positions,
                             const uint32_t * indices,
                             const float * texcoords,
                             const unsigned char * pixels,
                             int components,
                             const SbVec4f & color,
                             int width = 1,
                             int height = 1)
{
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
  command.material.diffuse = color;
  command.material.texture.pixels = pixels;
  command.material.texture.width = width;
  command.material.texture.height = height;
  command.material.texture.numComponents = components;
  return command;
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
  config.width = 32;
  config.height = 32;
  GLTestContext context;
  if (!context.initialize(config)) {
    return skip("core GLFW OpenGL context is unavailable");
  }

  const int glMajor = context.majorVersion();
  const int glMinor = context.minorVersion();
  const char * shadingLanguageVersion = reinterpret_cast<const char *>(
    glGetString(GL_SHADING_LANGUAGE_VERSION));
  int shadingLanguageMajor = 0;
  int shadingLanguageMinor = 0;
  const bool parsedShadingLanguageVersion =
    shadingLanguageVersion &&
    std::sscanf(shadingLanguageVersion, "%d.%d",
                &shadingLanguageMajor, &shadingLanguageMinor) == 2;
  const bool expectedBaseline =
    (glMajor > 3 || (glMajor == 3 && glMinor >= 3)) &&
    parsedShadingLanguageVersion &&
    (shadingLanguageMajor > 3 ||
     (shadingLanguageMajor == 3 && shadingLanguageMinor >= 30));
  if (!expectedBaseline) {
    std::cerr << "FAIL: retained renderer test requires OpenGL 3.3 / GLSL 330, got GL "
              << glMajor << "." << glMinor << " and GLSL "
              << (shadingLanguageVersion ? shadingLanguageVersion : "<unknown>")
              << std::endl;
    return 1;
  }

  SoGLRenderBackend backend;
  SoRenderBackendInitParams initparams;
  if (!backend.initialize(initparams)) {
    std::cerr << "FAIL: retained backend did not initialize on the verified "
              << "OpenGL 3.3/GLSL 330 context" << std::endl;
    return 1;
  }

  const uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };
  const float texcoords[] = {
    0.0f, 0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f
  };
  const float quads[][12] = {
    { -1.0f, -1.0f, 0.0f, -0.5f, -1.0f, 0.0f,
      -0.5f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f },
    { -0.5f, -1.0f, 0.0f,  0.0f, -1.0f, 0.0f,
       0.0f, 1.0f, 0.0f, -0.5f, 1.0f, 0.0f },
    {  0.0f, -1.0f, 0.0f,  0.5f, -1.0f, 0.0f,
       0.5f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f },
    {  0.5f, -1.0f, 0.0f,  1.0f, -1.0f, 0.0f,
       1.0f, 1.0f, 0.0f,  0.5f, 1.0f, 0.0f }
  };
  const unsigned char luminance[] = { 220 };
  const unsigned char luminanceAlpha[] = { 100, 255 };
  const unsigned char rgb[] = {
    0, 220, 0, 0, 220, 0, 0, 220, 0,
    0, 220, 0, 0, 220, 0, 0, 220, 0
  };
  const unsigned char rgba[] = { 0, 0, 220, 255 };

  SoDrawList drawlist;
  drawlist.addCommand(texturedQuad(quads[0], indices, texcoords,
                                   luminance, 1,
                                   SbVec4f(1.0f, 1.0f, 1.0f, 1.0f)));
  drawlist.addCommand(texturedQuad(quads[1], indices, texcoords,
                                   luminanceAlpha, 2,
                                   SbVec4f(1.0f, 1.0f, 1.0f, 1.0f)));
  drawlist.addCommand(texturedQuad(quads[2], indices, texcoords,
                                   rgb, 3,
                                   SbVec4f(1.0f, 1.0f, 1.0f, 1.0f),
                                   3, 2));
  drawlist.addCommand(texturedQuad(quads[3], indices, texcoords,
                                   rgba, 4,
                                   SbVec4f(1.0f, 1.0f, 1.0f, 1.0f)));

  const SoRenderParams params = renderParams();
  int result = 0;
  SoDrawList empty;
  if (!renderWithPlan(backend, empty, params)) {
    std::cerr << "FAIL: empty draw list was not accepted" << std::endl;
    result = 1;
  }

  if (!renderWithPlan(backend, drawlist, params)) {
    std::cerr << "FAIL: indexed draw-list execution failed" << std::endl;
    result = 1;
  }
  else {
    glFinish();
    const std::vector<uint8_t> pixels = readPixels(context);
    if (!nearColor(pixelAt(pixels, 4, 16), 220, 220, 220)) {
      std::cerr << "FAIL: L texture upload produced unexpected pixels" << std::endl;
      result = 1;
    }
    if (!nearColor(pixelAt(pixels, 12, 16), 100, 100, 100)) {
      std::cerr << "FAIL: LA texture upload produced unexpected pixels" << std::endl;
      result = 1;
    }
    if (!nearColor(pixelAt(pixels, 20, 16), 0, 220, 0)) {
      std::cerr << "FAIL: RGB texture upload produced unexpected pixels" << std::endl;
      result = 1;
    }
    if (!nearColor(pixelAt(pixels, 28, 16), 0, 0, 220)) {
      std::cerr << "FAIL: RGBA texture upload produced unexpected pixels" << std::endl;
      result = 1;
    }
  }

  // An unchanged DrawList must be safe to execute repeatedly without another
  // upload or a change in output.
  if (!renderWithPlan(backend, drawlist, params)) {
    std::cerr << "FAIL: repeated DrawList execution failed" << std::endl;
    result = 1;
  }

  // Explicit color-space semantics select the upload format, and form part
  // of persistent texture identity even when the producer keeps its key and
  // revision unchanged.
  const float fullQuad[] = {
    -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f,
     1.0f,  1.0f, 0.0f, -1.0f, 1.0f, 0.0f
  };
  const unsigned char middleGray[] = { 128, 128, 128 };
  SoDrawList colorSpaceDrawlist;
  SoRenderCommand colorSpaceCommand = texturedQuad(
    fullQuad, indices, texcoords, middleGray, 3,
    SbVec4f(1.0f, 1.0f, 1.0f, 1.0f));
  colorSpaceCommand.material.texture.cacheKey = 0x73u;
  colorSpaceCommand.material.texture.revision = 1;
  colorSpaceCommand.material.texture.colorSpace = SO_TEXTURE_COLORSPACE_LINEAR;
  colorSpaceDrawlist.addCommand(colorSpaceCommand);
  if (!renderWithPlan(backend, colorSpaceDrawlist, params)) {
    std::cerr << "FAIL: linear texture color space was not accepted"
              << std::endl;
    result = 1;
  }
  else {
    glFinish();
    const std::vector<uint8_t> pixels = readPixels(context);
    if (!nearColor(pixelAt(pixels, 16, 16), 128, 128, 128)) {
      std::cerr << "FAIL: linear texture upload changed texel interpretation"
                << std::endl;
      result = 1;
    }
  }

  colorSpaceDrawlist.clear();
  colorSpaceCommand.material.texture.colorSpace = SO_TEXTURE_COLORSPACE_SRGB;
  colorSpaceDrawlist.addCommand(colorSpaceCommand);
  if (!renderWithPlan(backend, colorSpaceDrawlist, params)) {
    std::cerr << "FAIL: sRGB texture color space was not accepted" << std::endl;
    result = 1;
  }
  else {
    glFinish();
    const std::vector<uint8_t> pixels = readPixels(context);
    if (!nearColor(pixelAt(pixels, 16, 16), 55, 55, 55)) {
      std::cerr << "FAIL: texture cache ignored changed color-space semantics"
                << std::endl;
      result = 1;
    }
  }

  // Identical indexed geometry with distinct transforms and unlit diffuse
  // colors should retain command identity while sharing one instanced draw.
  const float indexedInstanceQuad[] = {
    -0.35f, -0.35f, 0.0f,
     0.35f, -0.35f, 0.0f,
     0.35f,  0.35f, 0.0f,
    -0.35f,  0.35f, 0.0f
  };
  drawlist.clear();
  SoRenderCommand indexedInstance;
  indexedInstance.modelMatrix.setTranslate(SbVec3f(-0.5f, 0.0f, 0.0f));
  indexedInstance.geometry.topology = SO_TOPOLOGY_TRIANGLES;
  indexedInstance.geometry.vertexCount = 4;
  indexedInstance.geometry.indexCount = 6;
  indexedInstance.geometry.positions = indexedInstanceQuad;
  indexedInstance.geometry.indices = indices;
  indexedInstance.geometry.vertexStride = sizeof(float) * 3;
  indexedInstance.geometry.cacheKey = 0x100u;
  indexedInstance.geometry.revision = 1;
  indexedInstance.material.diffuse = SbVec4f(1.0f, 0.0f, 0.0f, 1.0f);
  drawlist.addCommand(indexedInstance);
  indexedInstance.modelMatrix.setTranslate(SbVec3f(0.5f, 0.0f, 0.0f));
  indexedInstance.material.diffuse = SbVec4f(0.0f, 0.0f, 1.0f, 1.0f);
  drawlist.addCommand(indexedInstance);
  if (!renderWithPlan(backend, drawlist, params)) {
    std::cerr << "FAIL: indexed instanced execution failed" << std::endl;
    result = 1;
  }
  else {
    glFinish();
    const SoRenderStatistics statistics = backend.getRenderStatistics();
    const std::vector<uint8_t> pixels = readPixels(context);
    if (statistics.drawCalls != 1 || statistics.instancedCommands != 2 ||
        statistics.drawCallsAvoided != 1) {
      std::cerr << "FAIL: indexed commands did not collapse into one draw"
                << std::endl;
      result = 1;
    }
    if (!nearColor(pixelAt(pixels, 8, 16), 255, 0, 0) ||
        !nearColor(pixelAt(pixels, 24, 16), 0, 0, 255)) {
      std::cerr << "FAIL: indexed instancing lost transform or color data"
                << std::endl;
      result = 1;
    }
  }

  // A shared texture and sampler are batch resources; transform and diffuse
  // color remain per-instance inputs.
  const unsigned char whiteTexel[] = { 255, 255, 255, 255 };
  SoRenderCommand texturedInstance = indexedInstance;
  texturedInstance.modelMatrix.setTranslate(SbVec3f(-0.5f, 0.0f, 0.0f));
  texturedInstance.geometry.cacheKey = 0x300u;
  texturedInstance.geometry.texcoords = texcoords;
  texturedInstance.geometry.texcoordStride = sizeof(float) * 4;
  texturedInstance.material.texture.pixels = whiteTexel;
  texturedInstance.material.texture.width = 1;
  texturedInstance.material.texture.height = 1;
  texturedInstance.material.texture.numComponents = 4;
  texturedInstance.material.texture.cacheKey = 0x400u;
  texturedInstance.material.texture.revision = 1;
  texturedInstance.material.diffuse = SbVec4f(1.0f, 0.0f, 0.0f, 1.0f);
  drawlist.clear();
  drawlist.addCommand(texturedInstance);
  texturedInstance.modelMatrix.setTranslate(SbVec3f(0.5f, 0.0f, 0.0f));
  texturedInstance.material.diffuse = SbVec4f(0.0f, 0.0f, 1.0f, 1.0f);
  drawlist.addCommand(texturedInstance);
  if (!renderWithPlan(backend, drawlist, params)) {
    std::cerr << "FAIL: textured instanced execution failed" << std::endl;
    result = 1;
  }
  else {
    glFinish();
    const SoRenderStatistics statistics = backend.getRenderStatistics();
    const std::vector<uint8_t> pixels = readPixels(context);
    if (statistics.drawCalls != 1 || statistics.instancedCommands != 2 ||
        !nearColor(pixelAt(pixels, 8, 16), 255, 0, 0) ||
        !nearColor(pixelAt(pixels, 24, 16), 0, 0, 255)) {
      std::cerr << "FAIL: textured commands lost batching or instance data"
                << std::endl;
      result = 1;
    }
  }

  // State or resource differences must terminate an instance batch. Keep
  // these cases together so additions to the eligibility rules remain easy
  // to audit against the state they are allowed to coalesce.
  const auto expectBatchBreak = [&](const SoRenderCommand & first,
                                    const SoRenderCommand & second,
                                    const char * boundary) {
    drawlist.clear();
    drawlist.addCommand(first);
    drawlist.addCommand(second);
    if (!renderWithPlan(backend, drawlist, params)) {
      std::cerr << "FAIL: " << boundary << " batch-boundary render failed"
                << std::endl;
      result = 1;
      return;
    }
    const SoRenderStatistics statistics = backend.getRenderStatistics();
    if (statistics.drawCalls != 2 || statistics.instancedBatches != 0) {
      std::cerr << "FAIL: instancing crossed the " << boundary
                << " boundary" << std::endl;
      result = 1;
    }
  };
  SoRenderCommand differentGeometry = indexedInstance;
  differentGeometry.geometry.cacheKey = 0x101u;
  expectBatchBreak(indexedInstance, differentGeometry, "geometry-resource");
  SoRenderCommand differentMaterial = indexedInstance;
  differentMaterial.material.shadingModel = SO_SHADING_LEGACY_GOURAUD;
  expectBatchBreak(indexedInstance, differentMaterial, "shading-model");
  SoRenderCommand differentDepth = indexedInstance;
  differentDepth.state.depth.func = SO_DEPTH_LESS;
  expectBatchBreak(indexedInstance, differentDepth, "depth-state");
  SoRenderCommand transparent = indexedInstance;
  transparent.opacityClass = SO_OPACITY_TRANSPARENT;
  transparent.material.opacity = 0.5f;
  transparent.material.diffuse[3] = 0.5f;
  transparent.state.blend.enabled = TRUE;
  expectBatchBreak(indexedInstance, transparent, "transparency");
  SoRenderCommand viewportOverride = indexedInstance;
  viewportOverride.state.raster.viewportOverride = TRUE;
  viewportOverride.state.raster.viewportEnabled = TRUE;
  viewportOverride.state.raster.viewportWidth = 32;
  viewportOverride.state.raster.viewportHeight = 32;
  expectBatchBreak(indexedInstance, viewportOverride, "viewport");
  SoRenderCommand differentTexture = texturedInstance;
  differentTexture.material.texture.cacheKey = 0x401u;
  expectBatchBreak(texturedInstance, differentTexture, "texture-resource");
  SoRenderCommand differentSampler = texturedInstance;
  differentSampler.material.texture.magFilter = SO_TEXTURE_FILTER_LINEAR;
  expectBatchBreak(texturedInstance, differentSampler, "texture-sampler");

  // Transparent instances must follow the planner's back-to-front order.
  // Compare the batch against the same draws forced through separate GPU
  // resources so this test detects an order change, not just a plausible
  // blended color.
  SoRenderCommand farTransparent = indexedInstance;
  farTransparent.modelMatrix.setTranslate(SbVec3f(0.0f, 0.0f, 0.5f));
  farTransparent.geometry.cacheKey = 0x200u;
  farTransparent.opacityClass = SO_OPACITY_TRANSPARENT;
  farTransparent.material.opacity = 0.5f;
  farTransparent.material.diffuse = SbVec4f(1.0f, 0.0f, 0.0f, 0.5f);
  farTransparent.state.depth.writeEnabled = FALSE;
  farTransparent.state.blend.enabled = TRUE;
  farTransparent.state.blend.srcRGBFactor = SO_BLEND_FACTOR_SRC_ALPHA;
  farTransparent.state.blend.dstRGBFactor =
    SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  farTransparent.state.blend.srcAlphaFactor = SO_BLEND_FACTOR_ONE;
  farTransparent.state.blend.dstAlphaFactor =
    SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  SoRenderCommand nearTransparent = farTransparent;
  nearTransparent.modelMatrix.setTranslate(SbVec3f(0.0f, 0.0f, -0.5f));
  nearTransparent.geometry.cacheKey = 0x201u;
  nearTransparent.material.diffuse = SbVec4f(0.0f, 0.0f, 1.0f, 0.5f);
  drawlist.clear();
  drawlist.addCommand(nearTransparent);
  drawlist.addCommand(farTransparent);
  std::vector<uint8_t> transparentReference;
  if (!renderWithPlan(backend, drawlist, params)) {
    std::cerr << "FAIL: transparent reference render failed" << std::endl;
    result = 1;
  }
  else {
    glFinish();
    transparentReference = readPixels(context);
    if (backend.getRenderStatistics().drawCalls != 2) {
      std::cerr << "FAIL: transparent reference was not independent"
                << std::endl;
      result = 1;
    }
  }
  nearTransparent.geometry.cacheKey = 0x202u;
  farTransparent.geometry.cacheKey = 0x202u;
  drawlist.clear();
  drawlist.addCommand(nearTransparent);
  drawlist.addCommand(farTransparent);
  if (!renderWithPlan(backend, drawlist, params)) {
    std::cerr << "FAIL: transparent instanced render failed" << std::endl;
    result = 1;
  }
  else {
    glFinish();
    const SoRenderStatistics statistics = backend.getRenderStatistics();
    const std::vector<uint8_t> pixels = readPixels(context);
    if (transparentReference.empty()) {
      std::cerr << "FAIL: transparent reference pixels are unavailable"
                << std::endl;
      result = 1;
    }
    else {
      const uint8_t * reference = pixelAt(transparentReference, 16, 16);
      const uint8_t * instanced = pixelAt(pixels, 16, 16);
      if (statistics.drawCalls != 1 || statistics.instancedCommands != 2 ||
          !nearColor(instanced, reference[0], reference[1], reference[2])) {
        std::cerr << "FAIL: transparent batch changed sorted blending"
                  << " (draws=" << statistics.drawCalls
                  << ", instances=" << statistics.instancedCommands
                  << ", reference=" << static_cast<int>(reference[0]) << ','
                  << static_cast<int>(reference[1]) << ','
                  << static_cast<int>(reference[2]) << ", instanced="
                  << static_cast<int>(instanced[0]) << ','
                  << static_cast<int>(instanced[1]) << ','
                  << static_cast<int>(instanced[2]) << ')' << std::endl;
        result = 1;
      }
    }
  }

  SoRenderCommand differentBlend = farTransparent;
  differentBlend.state.blend.dstRGBFactor = SO_BLEND_FACTOR_ONE;
  expectBatchBreak(farTransparent, differentBlend, "blend-state");
  SoRenderCommand differentOpacity = farTransparent;
  differentOpacity.material.opacity = 0.25f;
  expectBatchBreak(farTransparent, differentOpacity, "material-opacity");
  SoRenderCommand alphaTested = farTransparent;
  alphaTested.state.alphaTest.policy = SO_ALPHA_TEST_POLICY_EXPLICIT;
  alphaTested.state.alphaTest.function = SO_ALPHA_TEST_GREATER;
  alphaTested.state.alphaTest.reference = 0.25f;
  expectBatchBreak(farTransparent, alphaTested, "alpha-test");
  const unsigned char transparentTexel[] = { 255, 255, 255, 128 };
  SoRenderCommand texturedTransparent = farTransparent;
  texturedTransparent.material.texture.pixels = transparentTexel;
  texturedTransparent.material.texture.width = 1;
  texturedTransparent.material.texture.height = 1;
  texturedTransparent.material.texture.numComponents = 4;
  expectBatchBreak(farTransparent, texturedTransparent, "texture");

  drawlist.clear();
  drawlist.addCommand(farTransparent);
  SoDepthClearEvent transparentBarrier;
  transparentBarrier.sequence = 1;
  drawlist.addDepthClearEvent(transparentBarrier);
  drawlist.addCommand(nearTransparent);
  if (!renderWithPlan(backend, drawlist, params) ||
      backend.getRenderStatistics().drawCalls != 2) {
    std::cerr << "FAIL: instancing crossed a transparent depth segment"
              << std::endl;
    result = 1;
  }

  // A revision change replaces the shared indexed resource while preserving
  // batching. This catches stale EBO/VBO reuse after producer mutation.
  const float movedIndexedInstanceQuad[] = {
    -0.35f, 0.15f, 0.0f,
     0.35f, 0.15f, 0.0f,
     0.35f, 0.85f, 0.0f,
    -0.35f, 0.85f, 0.0f
  };
  drawlist.clear();
  indexedInstance.geometry.positions = movedIndexedInstanceQuad;
  indexedInstance.geometry.revision = 2;
  indexedInstance.modelMatrix.setTranslate(SbVec3f(-0.5f, 0.0f, 0.0f));
  indexedInstance.material.diffuse = SbVec4f(1.0f, 0.0f, 0.0f, 1.0f);
  drawlist.addCommand(indexedInstance);
  indexedInstance.modelMatrix.setTranslate(SbVec3f(0.5f, 0.0f, 0.0f));
  indexedInstance.material.diffuse = SbVec4f(0.0f, 0.0f, 1.0f, 1.0f);
  drawlist.addCommand(indexedInstance);
  if (!renderWithPlan(backend, drawlist, params)) {
    std::cerr << "FAIL: revised indexed instance render failed" << std::endl;
    result = 1;
  }
  else {
    glFinish();
    const SoRenderStatistics statistics = backend.getRenderStatistics();
    const std::vector<uint8_t> pixels = readPixels(context);
    if (statistics.drawCalls != 1 || statistics.instancedCommands != 2 ||
        !nearColor(pixelAt(pixels, 8, 24), 255, 0, 0) ||
        !nearColor(pixelAt(pixels, 24, 24), 0, 0, 255)) {
      std::cerr << "FAIL: revised indexed resource was stale or unbatched"
                << std::endl;
      result = 1;
    }
  }

  // Exercise unindexed geometry and the basic vertex-color path.
  const float triangle[] = {
    -0.8f, -0.8f, 0.0f,
     0.8f, -0.8f, 0.0f,
     0.0f,  0.8f, 0.0f
  };
  const float vertexColors[] = {
    0.0f, 1.0f, 0.0f, 1.0f,
    0.0f, 1.0f, 0.0f, 1.0f,
    0.0f, 1.0f, 0.0f, 1.0f
  };
  drawlist.clear();
  SoRenderCommand unindexed;
  unindexed.modelMatrix.makeIdentity();
  unindexed.geometry.topology = SO_TOPOLOGY_TRIANGLES;
  unindexed.geometry.vertexCount = 3;
  unindexed.geometry.positions = triangle;
  unindexed.geometry.vertexStride = sizeof(float) * 3;
  unindexed.geometry.colors = vertexColors;
  unindexed.material.diffuse = SbVec4f(1.0f, 0.0f, 0.0f, 1.0f);
  drawlist.addCommand(unindexed);
  if (!renderWithPlan(backend, drawlist, params)) {
    std::cerr << "FAIL: unindexed vertex-color execution failed" << std::endl;
    result = 1;
  }
  else {
    glFinish();
    const std::vector<uint8_t> pixels = readPixels(context);
    if (!nearColor(pixelAt(pixels, 16, 16), 0, 255, 0)) {
      std::cerr << "FAIL: vertex color was not used for unindexed geometry"
                << std::endl;
      result = 1;
    }
  }

  // A keyed geometry resource remains usable after the producer replaces its
  // frame storage. The second frame intentionally provides no CPU positions;
  // the backend must use the resource retained from the first frame.
  SoDrawList persistentDrawlist;
  SoRenderCommand persistentCommand = unindexed;
  persistentCommand.geometry.cacheKey = 0x42u;
  persistentCommand.geometry.revision = 1;
  persistentDrawlist.addCommand(persistentCommand);
  if (!renderWithPlan(backend, persistentDrawlist, params)) {
    std::cerr << "FAIL: keyed geometry first-frame execution failed" << std::endl;
    result = 1;
  }
  persistentDrawlist.clear();
  persistentCommand.geometry.positions = nullptr;
  persistentDrawlist.addCommand(persistentCommand);
  if (!renderWithPlan(backend, persistentDrawlist, params)) {
    std::cerr << "FAIL: keyed geometry was not retained across frames"
              << std::endl;
    result = 1;
  }
  else {
    glFinish();
    const std::vector<uint8_t> pixels = readPixels(context);
    if (!nearColor(pixelAt(pixels, 16, 16), 0, 255, 0)) {
      std::cerr << "FAIL: retained keyed geometry did not render from GPU data"
                << std::endl;
      result = 1;
    }
  }

  // clear() changes the generation. Replacing the command must not reuse the
  // previous frame's GPU data.
  const float replacement[] = {
    -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f
  };
  drawlist.clear();
  SoRenderCommand replaced;
  replaced.modelMatrix.makeIdentity();
  replaced.geometry.topology = SO_TOPOLOGY_TRIANGLES;
  replaced.geometry.vertexCount = 4;
  replaced.geometry.indexCount = 6;
  replaced.geometry.positions = replacement;
  replaced.geometry.indices = indices;
  replaced.geometry.vertexStride = sizeof(float) * 3;
  replaced.material.diffuse = SbVec4f(0.0f, 0.0f, 1.0f, 1.0f);
  drawlist.addCommand(replaced);
  if (!renderWithPlan(backend, drawlist, params)) {
    std::cerr << "FAIL: generation-invalidated DrawList execution failed" << std::endl;
    result = 1;
  }
  else {
    glFinish();
    const std::vector<uint8_t> pixels = readPixels(context);
    if (!nearColor(pixelAt(pixels, 16, 16), 0, 0, 255)) {
      std::cerr << "FAIL: clear()/generation change reused stale GPU data" << std::endl;
      result = 1;
    }
  }

  // The active context remains valid while normal GL destruction happens.
  backend.shutdown();
  if (backend.isInitialized()) {
    std::cerr << "FAIL: backend remained initialized after shutdown" << std::endl;
    result = 1;
  }
  return result;
}

int
main()
{
  const int result = runTest();
  SoDB::finish();
  return result;
}
