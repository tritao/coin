// src/rendering/SoGLRenderBackend.cpp

#include "rendering/SoGLRenderBackend.h"
#include "rendering/SoRenderIRP.h"
#include "rendering/SoVBO.h"
#include "CoinTracyConfig.h"

// GL_GEOMETRY_SHADER may not be defined in macOS legacy GL headers
#ifndef GL_GEOMETRY_SHADER
#define GL_GEOMETRY_SHADER 0x8DD9
#endif

// macOS: <OpenGL/gl.h> only provides APPLE-suffixed VAO functions, but
// Core Profile requires the standard names. Declare them explicitly —
// they exist in the framework regardless of which header is included.
// In Compatibility Profile, these resolve to the same entry points.
#if defined(__APPLE__) && !defined(glGenVertexArrays)
extern "C" {
void glGenVertexArrays(GLsizei n, GLuint * arrays);
void glBindVertexArray(GLuint array);
void glDeleteVertexArrays(GLsizei n, const GLuint * arrays);
}
#endif

#include <Inventor/SbBasic.h>
#include <Inventor/C/tidbits.h>
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/SbMatrix.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include <data/shaders/backend/BackendUnifiedFragment.h>
#include <data/shaders/backend/BackendUnifiedVertex.h>
#include <data/shaders/backend/BackendWideLineFragment.h>
#include <data/shaders/backend/BackendWideLineGeometry.h>
#include <data/shaders/backend/BackendWideLineVertex.h>
#include <data/shaders/backend/BackendWidePointFragment.h>
#include <data/shaders/backend/BackendWidePointGeometry.h>
#include <data/shaders/backend/BackendWidePointVertex.h>

#include "rendering/SoVertexLayout.h"
#include "shaders/SoGLShaderProgram.h"

// -----------------------------------------------------------------------
// Named constants — extracted from magic numbers throughout render()
// -----------------------------------------------------------------------

// Blinn-Phong lighting coefficients (used in GLSL shader source)
static constexpr float AMBIENT_COEFF  = 0.25f;
static constexpr float DIFFUSE_COEFF  = 0.85f;
static constexpr float SPECULAR_COEFF = 0.12f;
static constexpr float DEFAULT_SHININESS = 64.0f;

// Alpha thresholds for selection overlays
static constexpr float SELECTION_ALPHA = 0.5f;

// Texture alpha discard threshold (shader-side)
static constexpr float ALPHA_DISCARD_THRESHOLD = 0.3f;

// Minimum point size for point highlight/selection overlays. Keep this
// slightly larger than the default point style so selected vertices stay
// legible without inflating normal point rendering.
static constexpr float MIN_SELECTION_POINT_SIZE = 6.0f;

// Cache GC: entries unused for this many frames are destroyed
static constexpr int CACHE_UNUSED_FRAME_THRESHOLD = 3;

// Safety limit: skip commands with absurd vertex counts
static constexpr int MAX_VERTEX_COUNT = 10000000;

// Default pick line width / point size when no pick buffer exists
static constexpr float DEFAULT_PICK_SIZE = 7.0f;

// Maximum number of scene lights uploaded to the unified shader.
static constexpr int MAX_SHADER_LIGHTS = 8;

static const SoLightingData &
coin_fallback_lighting()
{
  static const SoLightingData lighting = []() {
    SoLightingData fallback;
    fallback.ambient.setValue(0.2f, 0.2f, 0.2f);
    SoLightData headlight;
    headlight.type = SO_LIGHT_DIRECTIONAL;
    headlight.color.setValue(1.0f, 1.0f, 1.0f);
    headlight.direction.setValue(0.0f, 0.0f, 1.0f);
    fallback.lights.push_back(headlight);
    return fallback;
  }();
  return lighting;
}

static SbVec2s
coin_command_viewport_size(const SoRenderCommand & cmd,
                           const SoRenderParams & params)
{
  if (cmd.pass == SO_RENDERPASS_OVERLAY &&
      cmd.state.raster.viewportEnabled &&
      cmd.state.raster.viewportWidth > 0 &&
      cmd.state.raster.viewportHeight > 0) {
    return SbVec2s(static_cast<short>(cmd.state.raster.viewportWidth),
                   static_cast<short>(cmd.state.raster.viewportHeight));
  }
  return params.viewport.getViewportSizePixels();
}

static void
coin_apply_default_viewport(const SoRenderParams & params)
{
  const SbVec2s & origin = params.viewport.getViewportOriginPixels();
  const SbVec2s & size = params.viewport.getViewportSizePixels();
  glViewport(origin[0], origin[1], size[0], size[1]);
}

static void
coin_apply_command_viewport(const SoRenderCommand & cmd,
                            const SoRenderParams & params)
{
  if (cmd.pass == SO_RENDERPASS_OVERLAY &&
      cmd.state.raster.viewportEnabled &&
      cmd.state.raster.viewportWidth > 0 &&
      cmd.state.raster.viewportHeight > 0) {
    glViewport(cmd.state.raster.viewportX,
               cmd.state.raster.viewportY,
               cmd.state.raster.viewportWidth,
               cmd.state.raster.viewportHeight);
    return;
  }

  coin_apply_default_viewport(params);
}

static void
coin_clear_overlay_depth(const SoRenderCommand & cmd,
                         const SoRenderParams & params)
{
  if (!cmd.state.raster.clearDepth) {
    return;
  }

  if (!(cmd.state.raster.viewportEnabled &&
        cmd.state.raster.viewportWidth > 0 &&
        cmd.state.raster.viewportHeight > 0)) {
    glClear(GL_DEPTH_BUFFER_BIT);
    return;
  }

  const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
  GLint scissorBox[4] = {0, 0, 0, 0};
  glGetIntegerv(GL_SCISSOR_BOX, scissorBox);

  glEnable(GL_SCISSOR_TEST);
  glScissor(cmd.state.raster.viewportX,
            cmd.state.raster.viewportY,
            cmd.state.raster.viewportWidth,
            cmd.state.raster.viewportHeight);
  glClear(GL_DEPTH_BUFFER_BIT);

  glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
  if (scissorEnabled) {
    glEnable(GL_SCISSOR_TEST);
  }
  else {
    glDisable(GL_SCISSOR_TEST);
  }
  coin_apply_default_viewport(params);
}

static GLuint
coin_compile_shader(GLenum type, const char * source)
{
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint status = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length > 0) {
      std::string log(length, '\0');
      glGetShaderInfoLog(shader, length, &length, &log[0]);
      SoDebugError::postInfo("SoGLRenderBackend::compileShader", "%s", log.c_str());
    }
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static GLuint
coin_link_program(GLuint vs, GLuint fs)
{
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint status = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    if (length > 0) {
      std::string log(length, '\0');
      glGetProgramInfoLog(program, length, &length, &log[0]);
      SoDebugError::postInfo("SoGLRenderBackend::linkProgram", "%s", log.c_str());
    }
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

namespace {

inline GLenum topologyToGL(SoPrimitiveTopology topology) {
  switch (topology) {
  case SO_TOPOLOGY_POINTS: return GL_POINTS;
  case SO_TOPOLOGY_LINES: return GL_LINES;
  case SO_TOPOLOGY_TRIANGLES: return GL_TRIANGLES;
  case SO_TOPOLOGY_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
  case SO_TOPOLOGY_LINE_STRIP: return GL_LINE_STRIP;
  default: return GL_TRIANGLES;
  }
}

inline GLenum depthFunctionToGL(SoDepthFunction function)
{
  switch (function) {
    case SO_DEPTH_NEVER: return GL_NEVER;
    case SO_DEPTH_ALWAYS: return GL_ALWAYS;
    case SO_DEPTH_LESS: return GL_LESS;
    case SO_DEPTH_LEQUAL: return GL_LEQUAL;
    case SO_DEPTH_EQUAL: return GL_EQUAL;
    case SO_DEPTH_GEQUAL: return GL_GEQUAL;
    case SO_DEPTH_GREATER: return GL_GREATER;
    case SO_DEPTH_NOTEQUAL: return GL_NOTEQUAL;
    default: return GL_LEQUAL;
  }
}

inline bool textureFilterUsesMipmaps(const SoTextureFilter filter)
{
  return filter == SO_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST
      || filter == SO_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST
      || filter == SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR
      || filter == SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR;
}

inline GLenum textureFilterToGL(const SoTextureFilter filter)
{
  switch (filter) {
    case SO_TEXTURE_FILTER_LINEAR:
      return GL_LINEAR;
    case SO_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
      return GL_NEAREST_MIPMAP_NEAREST;
    case SO_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
      return GL_LINEAR_MIPMAP_NEAREST;
    case SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
      return GL_NEAREST_MIPMAP_LINEAR;
    case SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
      return GL_LINEAR_MIPMAP_LINEAR;
    case SO_TEXTURE_FILTER_NEAREST:
    default:
      return GL_NEAREST;
  }
}

inline GLenum textureWrapToGL(const SoTextureWrap wrap)
{
  switch (wrap) {
    case SO_TEXTURE_WRAP_REPEAT:
      return GL_REPEAT;
    case SO_TEXTURE_WRAP_CLAMP_TO_BORDER:
      return GL_CLAMP_TO_BORDER;
    case SO_TEXTURE_WRAP_CLAMP_TO_EDGE:
    default:
      return GL_CLAMP_TO_EDGE;
  }
}

inline void applyTextureSampler(const SoTextureData & texture)
{
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  textureFilterToGL(texture.minFilter));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  textureFilterToGL(texture.magFilter));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                  textureWrapToGL(texture.wrapS));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                  textureWrapToGL(texture.wrapT));
}

inline GLenum blendFactorToGL(const SoBlendFactor factor)
{
  switch (factor) {
    case SO_BLEND_FACTOR_ZERO: return GL_ZERO;
    case SO_BLEND_FACTOR_ONE: return GL_ONE;
    case SO_BLEND_FACTOR_SRC_COLOR: return GL_SRC_COLOR;
    case SO_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
    case SO_BLEND_FACTOR_DST_COLOR: return GL_DST_COLOR;
    case SO_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return GL_ONE_MINUS_DST_COLOR;
    case SO_BLEND_FACTOR_SRC_ALPHA: return GL_SRC_ALPHA;
    case SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case SO_BLEND_FACTOR_DST_ALPHA: return GL_DST_ALPHA;
    case SO_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case SO_BLEND_FACTOR_CONSTANT_COLOR: return GL_CONSTANT_COLOR;
    case SO_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
      return GL_ONE_MINUS_CONSTANT_COLOR;
    case SO_BLEND_FACTOR_CONSTANT_ALPHA: return GL_CONSTANT_ALPHA;
    case SO_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
      return GL_ONE_MINUS_CONSTANT_ALPHA;
    case SO_BLEND_FACTOR_SRC_ALPHA_SATURATE: return GL_SRC_ALPHA_SATURATE;
    default: return GL_ONE;
  }
}

inline GLenum blendEquationToGL(const SoBlendEquation equation)
{
  switch (equation) {
    case SO_BLEND_EQUATION_SUBTRACT: return GL_FUNC_SUBTRACT;
    case SO_BLEND_EQUATION_REVERSE_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
    case SO_BLEND_EQUATION_MIN: return GL_MIN;
    case SO_BLEND_EQUATION_MAX: return GL_MAX;
    case SO_BLEND_EQUATION_ADD:
    default: return GL_FUNC_ADD;
  }
}

// Apply one command's complete blend contract. Pass setup may establish a
// useful default for legacy GL, but this is the authoritative per-command
// state and is deliberately the only helper used by drawCommand().
inline void coin_apply_blend_state(const SoBlendState & blend)
{
  if (!blend.enabled) {
    glDisable(GL_BLEND);
    return;
  }

  glEnable(GL_BLEND);
  const GLenum srcRGB = blendFactorToGL(blend.srcRGBFactor);
  const GLenum dstRGB = blendFactorToGL(blend.dstRGBFactor);
  const GLenum srcAlpha = blendFactorToGL(blend.srcAlphaFactor);
  const GLenum dstAlpha = blendFactorToGL(blend.dstAlphaFactor);
  glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);

  const GLenum rgbEquation = blendEquationToGL(blend.rgbEquation);
  const GLenum alphaEquation = blendEquationToGL(blend.alphaEquation);
  glBlendEquationSeparate(rgbEquation, alphaEquation);
}

inline void coin_apply_depth_state(const SoRenderCommand & cmd)
{
  if (cmd.state.depth.enabled) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(depthFunctionToGL(cmd.state.depth.func));
  } else {
    glDisable(GL_DEPTH_TEST);
  }

  // Legacy transparent passes intentionally keep the depth buffer read-only;
  // all other command stages honor the captured write policy.
  const bool transparentPass = cmd.pass == SO_RENDERPASS_TRANSPARENT &&
                               cmd.stage != SoRenderStage::Foreground;
  const bool writeDepth = cmd.state.depth.writeEnabled && !transparentPass;
  glDepthMask(writeDepth ? GL_TRUE : GL_FALSE);
}

} // namespace

// -----------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------

SoGLRenderBackend::SoGLRenderBackend()
{
  std::memset(&this->storedparams, 0, sizeof(SoRenderBackendInitParams));
}

SoGLRenderBackend::~SoGLRenderBackend()
{
  if (this->isInitialized()) {
    this->shutdown();
  }
}

const char *
SoGLRenderBackend::getName() const
{
  return "GLRenderBackend";
}

// -----------------------------------------------------------------------
// Initialize / Shutdown
// -----------------------------------------------------------------------

SbBool
SoGLRenderBackend::initialize(const SoRenderBackendInitParams & params)
{
  if (this->isInitialized()) {
    return TRUE;
  }

  this->storedparams = params;
  this->setInitParams(params);

  // Fault injection for the Coin render-manager fallback test. This is
  // intentionally an environment hook so the test can exercise the real
  // backend initialization boundary without exposing a test-only API.
  const char * failInit = coin_getenv("COIN_TEST_FAIL_DRAW_LIST_INITIALIZATION");
  if (failInit && failInit[0] != '0' && failInit[0] != '\0') {
    this->emitError("forced DrawList backend initialization failure");
    return FALSE;
  }

  char buffer[128];
  std::snprintf(buffer, sizeof(buffer),
                "target=%dx%d samples=%d id=%u",
                params.targetInfo.size[0], params.targetInfo.size[1],
                params.targetInfo.samples, params.targetInfo.targetId);
  if (coin_render_ir_trace_enabled()) {
    this->emitLog(buffer);
  }

  if (!this->createShaders()) {
    this->emitError("failed to create ModernGL shader");
    return FALSE;
  }

  // Use the same range queried by SoGLLineWidthElement in LegacyGL.  Native
  // DrawList lines therefore receive the same driver clamping behavior.
  GLfloat lineWidthRange[2] = {1.0f, 1.0f};
  glGetFloatv(GL_LINE_WIDTH_RANGE, lineWidthRange);
  this->nativeLineWidthMax = std::max(lineWidthRange[1], 1.0f);

  // Cache attribute locations
  this->posLoc = glGetAttribLocation(this->shaderProgram, "a_position");
  this->normLoc = glGetAttribLocation(this->shaderProgram, "a_normal");
  this->colorLoc = glGetAttribLocation(this->shaderProgram, "a_color");

  // Initialize GPU pick buffer
  pickBuffer = std::make_unique<SoIDPickBuffer>();
  if (!pickBuffer->initialize()) {
    this->emitLog("ID pick buffer initialization failed (picking disabled)");
    pickBuffer.reset();
  }

  this->setInitialized(TRUE);
  return TRUE;
}

void
SoGLRenderBackend::shutdown()
{
  pickBuffer.reset();

  // Destroy all cached GPU resources
  for (auto & entry : gpuCache) {
    destroyCacheEntry(entry);
  }
  gpuCache.clear();
  ptrToCacheIndex.clear();

  if (this->shaderProgram) {
    glDeleteProgram(this->shaderProgram);
    this->shaderProgram = 0;
  }
  if (this->lineShaderProgram) {
    glDeleteProgram(this->lineShaderProgram);
    this->lineShaderProgram = 0;
  }
  if (this->pointShaderProgram) {
    glDeleteProgram(this->pointShaderProgram);
    this->pointShaderProgram = 0;
  }
  this->setInitialized(FALSE);
  if (coin_render_ir_trace_enabled()) {
    this->emitLog("shutdown");
  }
}

void
SoGLRenderBackend::discard()
{
  if (!this->isInitialized()) {
    return;
  }

  if (pickBuffer) {
    pickBuffer->discard();
    pickBuffer.reset();
  }

  gpuCache.clear();
  ptrToCacheIndex.clear();

  this->shaderProgram = 0;
  this->lineShaderProgram = 0;
  this->pointShaderProgram = 0;
  this->currentFrame = 0;
  this->pickBufferDirty = true;
  this->lastPickLUTSize = 0;
  this->matricesInitialized = false;
  this->setInitialized(FALSE);
}

// -----------------------------------------------------------------------
// GPU Cache Management
// -----------------------------------------------------------------------

CachedGPUCommand &
SoGLRenderBackend::getOrCreateCache(const float * posPtr, const uint32_t * idxPtr)
{
  CacheKey key{posPtr, idxPtr};
  auto it = ptrToCacheIndex.find(key);
  if (it != ptrToCacheIndex.end()) {
    return gpuCache[it->second];
  }
  int idx = static_cast<int>(gpuCache.size());
  gpuCache.emplace_back();
  ptrToCacheIndex[key] = idx;
  return gpuCache[idx];
}

void
SoGLRenderBackend::uploadGeometry(CachedGPUCommand & entry,
                                  const SoRenderCommand & cmd)
{
  ZoneScopedN("uploadGeometry");
  GLsizei stride = static_cast<GLsizei>(
    cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);

  // Position VBO
  if (entry.posVBO == 0) glGenBuffers(1, &entry.posVBO);
  glBindBuffer(GL_ARRAY_BUFFER, entry.posVBO);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(cmd.geometry.vertexCount) * stride,
               cmd.geometry.positions, GL_STATIC_DRAW);

  // Normal VBO — may be smaller than position VBO for BRep shapes
  // (coordinate node includes edge/point vertices that lack normals)
  if (cmd.geometry.normals && cmd.geometry.normalCount > 0) {
    if (entry.normVBO == 0) glGenBuffers(1, &entry.normVBO);
    glBindBuffer(GL_ARRAY_BUFFER, entry.normVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cmd.geometry.normalCount) * stride,
                 cmd.geometry.normals, GL_STATIC_DRAW);
  }

  // Per-vertex color VBO (RGBA float, 4 components per vertex)
  if (cmd.geometry.colors && cmd.geometry.vertexCount > 0) {
    if (entry.colorVBO == 0) glGenBuffers(1, &entry.colorVBO);
    glBindBuffer(GL_ARRAY_BUFFER, entry.colorVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 cmd.geometry.vertexCount * sizeof(float) * 4,
                 cmd.geometry.colors, GL_STATIC_DRAW);
  }
  else if (entry.colorVBO != 0) {
    glDeleteBuffers(1, &entry.colorVBO);
    entry.colorVBO = 0;
  }

  // Texcoord VBO + texture upload (for SoImage etc.)
  if (cmd.geometry.texcoords && cmd.material.texture.pixels
      && cmd.geometry.vertexCount > 0) {
    // Texcoord VBO — extract vec2 from vec4 texcoords
    if (entry.texcoordVBO == 0) glGenBuffers(1, &entry.texcoordVBO);
    glBindBuffer(GL_ARRAY_BUFFER, entry.texcoordVBO);
    uint32_t tcStride = cmd.geometry.texcoordStride
      ? cmd.geometry.texcoordStride : sizeof(float) * 4;
    if (tcStride == sizeof(float) * 4) {
      std::vector<float> tc2(cmd.geometry.vertexCount * 2);
      const float * src = cmd.geometry.texcoords;
      for (uint32_t i = 0; i < cmd.geometry.vertexCount; i++) {
        tc2[i * 2] = src[i * 4];
        tc2[i * 2 + 1] = src[i * 4 + 1];
      }
      glBufferData(GL_ARRAY_BUFFER, tc2.size() * sizeof(float),
                   tc2.data(), GL_STATIC_DRAW);
    } else {
      glBufferData(GL_ARRAY_BUFFER,
                   cmd.geometry.vertexCount * sizeof(float) * 2,
                   cmd.geometry.texcoords, GL_STATIC_DRAW);
    }

    // Upload texture — expand 1/2-component to RGBA on CPU to avoid
    // GL_LUMINANCE/GL_LUMINANCE_ALPHA which are removed in Core Profile.
    if (entry.textureId == 0) glGenTextures(1, &entry.textureId);
    glBindTexture(GL_TEXTURE_2D, entry.textureId);
    int nc = cmd.material.texture.numComponents;
    int tw = cmd.material.texture.width;
    int th = cmd.material.texture.height;
    const unsigned char * src = cmd.material.texture.pixels;
    std::vector<unsigned char> expanded;
    if (nc == 1 || nc == 2) {
      int npx = tw * th;
      expanded.resize(npx * 4);
      for (int px = 0; px < npx; px++) {
        unsigned char lum = src[px * nc];
        unsigned char alpha = (nc == 2) ? src[px * nc + 1] : 255;
        expanded[px * 4]     = lum;
        expanded[px * 4 + 1] = lum;
        expanded[px * 4 + 2] = lum;
        expanded[px * 4 + 3] = alpha;
      }
      src = expanded.data();
      nc = 4;
    }
    GLenum fmt = (nc == 3) ? GL_RGB : GL_RGBA;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, tw, th, 0, fmt,
                 GL_UNSIGNED_BYTE, src);
    entry.textureHasMipmaps = false;
    if (textureFilterUsesMipmaps(cmd.material.texture.minFilter)) {
      glGenerateMipmap(GL_TEXTURE_2D);
      entry.textureHasMipmaps = true;
    }
    applyTextureSampler(cmd.material.texture);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  else {
    // No texture on this command — clean up stale texture state from
    // a previous command that used the same cache entry (pool address reuse).
    if (entry.textureId) {
      glDeleteTextures(1, &entry.textureId);
      entry.textureId = 0;
    }
    entry.textureHasMipmaps = false;
    if (entry.texcoordVBO) {
      glDeleteBuffers(1, &entry.texcoordVBO);
      entry.texcoordVBO = 0;
    }
  }

  // Index VBO
  if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
    if (entry.idxVBO == 0) glGenBuffers(1, &entry.idxVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, entry.idxVBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 cmd.geometry.indexCount * sizeof(uint32_t),
                 cmd.geometry.indices, GL_STATIC_DRAW);
  }

  // Per-vertex cumulative distance VBO for shader-based line stipple.
  // Computed for all line topologies so stipple works on curves.
  if (cmd.geometry.topology == SO_TOPOLOGY_LINES
      || cmd.geometry.topology == SO_TOPOLOGY_LINE_STRIP) {
    uint32_t vCount = cmd.geometry.vertexCount;
    std::vector<float> dist(vCount, 0.0f);
    const float * pos = cmd.geometry.positions;
    GLsizei bstride = static_cast<GLsizei>(
      cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);
    int floatStride = bstride / static_cast<int>(sizeof(float));

    if (cmd.geometry.topology == SO_TOPOLOGY_LINE_STRIP) {
      // Cumulative distance from vertex 0
      if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
        const uint32_t * idx = cmd.geometry.indices;
        for (uint32_t i = 1; i < cmd.geometry.indexCount; i++) {
          const float * p0 = pos + idx[i - 1] * floatStride;
          const float * p1 = pos + idx[i] * floatStride;
          float dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
          dist[idx[i]] = dist[idx[i - 1]] + std::sqrt(dx*dx + dy*dy + dz*dz);
        }
      } else {
        for (uint32_t i = 1; i < vCount; i++) {
          const float * p0 = pos + (i - 1) * floatStride;
          const float * p1 = pos + i * floatStride;
          float dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
          dist[i] = dist[i - 1] + std::sqrt(dx*dx + dy*dy + dz*dz);
        }
      }
    } else {
      // GL_LINES: each pair starts at 0, ends at segment length
      if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
        const uint32_t * idx = cmd.geometry.indices;
        for (uint32_t i = 0; i + 1 < cmd.geometry.indexCount; i += 2) {
          const float * p0 = pos + idx[i] * floatStride;
          const float * p1 = pos + idx[i + 1] * floatStride;
          float dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
          dist[idx[i]] = 0.0f;
          dist[idx[i + 1]] = std::sqrt(dx*dx + dy*dy + dz*dz);
        }
      } else {
        for (uint32_t i = 0; i + 1 < vCount; i += 2) {
          const float * p0 = pos + i * floatStride;
          const float * p1 = pos + (i + 1) * floatStride;
          float dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
          dist[i] = 0.0f;
          dist[i + 1] = std::sqrt(dx*dx + dy*dy + dz*dz);
        }
      }
    }

    if (entry.lineDistVBO == 0) glGenBuffers(1, &entry.lineDistVBO);
    glBindBuffer(GL_ARRAY_BUFFER, entry.lineDistVBO);
    glBufferData(GL_ARRAY_BUFFER, vCount * sizeof(float),
                 dist.data(), GL_STATIC_DRAW);
  }
  else if (entry.lineDistVBO) {
    glDeleteBuffers(1, &entry.lineDistVBO);
    entry.lineDistVBO = 0;
  }

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  // Update cache keys (generation is set by the caller after upload)
  entry.posKey = cmd.geometry.positions;
  entry.normKey = cmd.geometry.normals;
  entry.colorKey = cmd.geometry.colors;
  entry.idxKey = cmd.geometry.indices;
  entry.vertexCount = cmd.geometry.vertexCount;
  entry.indexCount = cmd.geometry.indexCount;
  entry.vertexStride = static_cast<uint32_t>(stride);
}

void
SoGLRenderBackend::setupVisualVAO(CachedGPUCommand & entry,
                                  const SoRenderCommand & cmd)
{
  if (entry.vao == 0) glGenVertexArrays(1, &entry.vao);
  glBindVertexArray(entry.vao);

  GLsizei stride = static_cast<GLsizei>(entry.vertexStride);

  // Position attribute
  if (this->posLoc >= 0 && entry.posVBO) {
    glBindBuffer(GL_ARRAY_BUFFER, entry.posVBO);
    glEnableVertexAttribArray(this->posLoc);
    glVertexAttribPointer(this->posLoc, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
  }

  // Normal attribute
  if (this->normLoc >= 0) {
    if (entry.normVBO) {
      glBindBuffer(GL_ARRAY_BUFFER, entry.normVBO);
      glEnableVertexAttribArray(this->normLoc);
      glVertexAttribPointer(this->normLoc, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    }
    else {
      glDisableVertexAttribArray(this->normLoc);
      glVertexAttrib3f(this->normLoc, 0.0f, 0.0f, 1.0f);
    }
  }

  // Per-vertex color attribute
  if (this->colorLoc >= 0) {
    if (entry.colorVBO) {
      glBindBuffer(GL_ARRAY_BUFFER, entry.colorVBO);
      glEnableVertexAttribArray(this->colorLoc);
      glVertexAttribPointer(this->colorLoc, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    else {
      glDisableVertexAttribArray(this->colorLoc);
      glVertexAttrib4f(this->colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    }
  }

  // Line distance attribute (cumulative object-space distance for stipple)
  if (this->lineDistLoc >= 0) {
    if (entry.lineDistVBO) {
      glBindBuffer(GL_ARRAY_BUFFER, entry.lineDistVBO);
      glEnableVertexAttribArray(this->lineDistLoc);
      glVertexAttribPointer(this->lineDistLoc, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    else {
      glDisableVertexAttribArray(this->lineDistLoc);
      glVertexAttrib1f(this->lineDistLoc, 0.0f);
    }
  }

  // Texcoord attribute (for textured commands — billboard/world-space)
  if (this->texcoordLoc >= 0) {
    if (entry.texcoordVBO) {
      glBindBuffer(GL_ARRAY_BUFFER, entry.texcoordVBO);
      glEnableVertexAttribArray(this->texcoordLoc);
      glVertexAttribPointer(this->texcoordLoc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    else {
      glDisableVertexAttribArray(this->texcoordLoc);
      glVertexAttrib2f(this->texcoordLoc, 0.0f, 0.0f);
    }
  }

  // Index buffer
  if (entry.idxVBO) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, entry.idxVBO);
  }

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void
SoGLRenderBackend::destroyCacheEntry(CachedGPUCommand & entry)
{
  if (entry.posVBO) { glDeleteBuffers(1, &entry.posVBO); entry.posVBO = 0; }
  if (entry.normVBO) { glDeleteBuffers(1, &entry.normVBO); entry.normVBO = 0; }
  if (entry.colorVBO) { glDeleteBuffers(1, &entry.colorVBO); entry.colorVBO = 0; }
  if (entry.texcoordVBO) { glDeleteBuffers(1, &entry.texcoordVBO); entry.texcoordVBO = 0; }
  if (entry.lineDistVBO) { glDeleteBuffers(1, &entry.lineDistVBO); entry.lineDistVBO = 0; }
  if (entry.textureId) { glDeleteTextures(1, &entry.textureId); entry.textureId = 0; }
  entry.textureHasMipmaps = false;
  if (entry.idxVBO) { glDeleteBuffers(1, &entry.idxVBO); entry.idxVBO = 0; }
  if (entry.vao) { glDeleteVertexArrays(1, &entry.vao); entry.vao = 0; }
  if (entry.idVAO) { glDeleteVertexArrays(1, &entry.idVAO); entry.idVAO = 0; }
}

void
SoGLRenderBackend::gcStaleEntries(int frame)
{
  // Remove entries unused for 3+ frames
  // Build list of stale pointer keys, then erase from map and mark entries dead
  for (int i = 0; i < static_cast<int>(gpuCache.size()); i++) {
    auto & entry = gpuCache[i];
    if (entry.posVBO == 0) continue;  // already dead
    if (frame - entry.lastUsedFrame > CACHE_UNUSED_FRAME_THRESHOLD) {
      ptrToCacheIndex.erase(CacheKey{entry.posKey, entry.idxKey});
      destroyCacheEntry(entry);
      entry.posKey = nullptr;
      entry.idxKey = nullptr;
    }
  }
}

const CachedGPUCommand *
SoGLRenderBackend::getCachedCommand(int cmdIndex) const
{
  // cmdIndex maps to draw list position; we need to look up by pointer
  // This method is called by the ID pass which iterates draw list commands
  // The caller should use the position pointer to find the cache entry
  (void)cmdIndex;
  return nullptr;  // Use ptrToCacheIndex directly instead
}

// -----------------------------------------------------------------------
// Draw Command — per-command GL state, draw call, state restore
// -----------------------------------------------------------------------

void
SoGLRenderBackend::drawCommand(const SoDrawList & drawlist,
                               const SoRenderCommand & cmd,
                               const SbMat & viewMat,
                               const SbMat & projMat,
                               const SoRenderParams & params)
{
  if (cmd.geometry.vertexCount == 0 || !cmd.geometry.positions) return;
  if (cmd.geometry.vertexCount > MAX_VERTEX_COUNT) return;
  if (cmd.geometry.indexCount > 0 && !cmd.geometry.indices) return;

  auto it = ptrToCacheIndex.find(CacheKey{cmd.geometry.positions, cmd.geometry.indices});
  if (it == ptrToCacheIndex.end()) return;
  const CachedGPUCommand & entry = gpuCache[it->second];
  if (entry.vao == 0) return;

  coin_apply_command_viewport(cmd, params);

  coin_apply_blend_state(cmd.state.blend);
  coin_apply_depth_state(cmd);

  // Per-command model matrix; view/proj from params (auto-clipped) for
  // main scene, or per-command for overlay/background (different camera).
  SbMat modelMat;
  cmd.modelMatrix.getValue(modelMat);
  glUniformMatrix4fv(this->uModelLocation, 1, GL_FALSE, &modelMat[0][0]);
  if (cmd.pass == SO_RENDERPASS_OVERLAY) {
    SbMat cmdViewMat, cmdProjMat;
    cmd.viewMatrix.getValue(cmdViewMat);
    cmd.projMatrix.getValue(cmdProjMat);
    glUniformMatrix4fv(this->uViewLocation, 1, GL_FALSE, &cmdViewMat[0][0]);
    glUniformMatrix4fv(this->uProjLocation, 1, GL_FALSE, &cmdProjMat[0][0]);
  } else {
    glUniformMatrix4fv(this->uViewLocation, 1, GL_FALSE, &viewMat[0][0]);
    glUniformMatrix4fv(this->uProjLocation, 1, GL_FALSE, &projMat[0][0]);
  }

  // Per-command color — use vertex colors if available
  bool hasVertexColors = (entry.colorVBO != 0);
  glUniform1f(this->uUseVertexColorLocation, hasVertexColors ? 1.0f : 0.0f);
  const SbVec4f & diffuse = cmd.material.diffuse;
  glUniform4f(this->uColorLocation,
              diffuse[0], diffuse[1], diffuse[2], diffuse[3]);
  glUniform1i(this->uAlphaTestPolicyLocation,
              static_cast<GLint>(cmd.state.alphaTest.policy));
  glUniform1i(this->uAlphaTestFunctionLocation,
              static_cast<GLint>(cmd.state.alphaTest.function));
  glUniform1f(this->uAlphaTestReferenceLocation,
              cmd.state.alphaTest.reference);
  glUniform1f(this->uVertexColorAlphaIncludesOpacityLocation,
              cmd.material.vertexColorAlphaIncludesOpacity ? 1.0f : 0.0f);
  glUniform1i(this->uShadingModelLocation,
              static_cast<GLint>(cmd.material.shadingModel));
  const SbVec4f & ambient = cmd.material.ambient;
  glUniform3f(this->uMaterialAmbientLocation,
              ambient[0], ambient[1], ambient[2]);
  const SbVec4f & specular = cmd.material.specular;
  glUniform3f(this->uMaterialSpecularLocation,
              specular[0], specular[1], specular[2]);
  glUniform1f(this->uMaterialShininessLocation, cmd.material.shininess);
  this->applyLighting(drawlist, cmd);

  GLenum prim = topologyToGL(cmd.geometry.topology);

  // Per-command backface culling
  if (cmd.state.raster.cullMode != 0) {
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
  }

  // Flat (unlit) rendering for points, lines, and BASE_COLOR materials.
  // Points/lines have zero normals; BASE_COLOR materials use emissive
  // as the display color (e.g. rotation center sphere, annotations).
  bool flatColor = (prim == GL_POINTS || prim == GL_LINES || prim == GL_LINE_STRIP
                    || cmd.material.shadingModel == SO_SHADING_UNLIT
                    || (cmd.material.featureFlags & SO_FEAT_BASE_COLOR));
  glUniform1f(this->uRenderModeLocation, flatColor ? 1.0f : 0.0f);


  // Per-command emissive color (added to lighting result)
  const SbVec4f & ec = cmd.material.emissive;
  glUniform3f(this->uEmissiveColorLocation, ec[0], ec[1], ec[2]);

  // Wireframe draw style: render triangles as lines
  uint8_t fillMode = cmd.state.raster.fillMode;
  if (fillMode == 1 && (prim == GL_TRIANGLES || prim == GL_TRIANGLE_STRIP)) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  }
  else if (fillMode == 2 && (prim == GL_TRIANGLES || prim == GL_TRIANGLE_STRIP)) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
  }

  float dpr = params.devicePixelRatio;
  if (dpr < 1.0f) dpr = 1.0f;
  bool usePointShader = false;
  if (prim == GL_POINTS || fillMode == 2) {
    float ps = cmd.state.raster.pointSize;
    if (ps < 1.0f) ps = cmd.state.raster.lineWidth;
    float pointSize = std::max(ps, 1.0f) * dpr;
    if (prim == GL_POINTS && this->pointShaderProgram) {
      usePointShader = true;
      const bool roundPoints =
        cmd.state.raster.pointShape == SO_POINT_SHAPE_ROUND || params.pointSmoothing;
      this->bindPointShader(cmd,
                            viewMat,
                            projMat,
                            diffuse,
                            entry.colorVBO != 0,
                            roundPoints,
                            pointSize,
                            coin_command_viewport_size(cmd, params));
    } else {
      glPointSize(pointSize);
      glUniform1f(this->uRenderModeLocation, 1.0f);
    }
  }
  bool useLineShader = false;
  const bool linePrimitive = prim == GL_LINES || prim == GL_LINE_STRIP;
  if (linePrimitive || fillMode == 1) {
    float lw = std::max(cmd.state.raster.lineWidth, 1.0f) * dpr;
    if (linePrimitive && this->shouldUseWideLineShader(lw)) {
      // Switch actual line primitives to the geometry-based width expansion
      // shader. Wireframe triangle commands stay on the regular triangle
      // path: the wide-line shader accepts GL_LINES input only.
      useLineShader = true;
      glUseProgram(this->lineShaderProgram);
      SbMat modelMat2;
      cmd.modelMatrix.getValue(modelMat2);
      glUniformMatrix4fv(this->lineUModelLocation, 1, GL_FALSE, &modelMat2[0][0]);
      if (cmd.pass == SO_RENDERPASS_OVERLAY) {
        SbMat cmdV, cmdP;
        cmd.viewMatrix.getValue(cmdV);
        cmd.projMatrix.getValue(cmdP);
        glUniformMatrix4fv(this->lineUViewLocation, 1, GL_FALSE, &cmdV[0][0]);
        glUniformMatrix4fv(this->lineUProjLocation, 1, GL_FALSE, &cmdP[0][0]);
      } else {
        glUniformMatrix4fv(this->lineUViewLocation, 1, GL_FALSE, &viewMat[0][0]);
        glUniformMatrix4fv(this->lineUProjLocation, 1, GL_FALSE, &projMat[0][0]);
      }
      bool hasVC = (entry.colorVBO != 0);
      glUniform1f(this->lineUUseVertexColorLocation, hasVC ? 1.0f : 0.0f);
      glUniform4f(this->lineUColorLocation,
                  diffuse[0], diffuse[1], diffuse[2], diffuse[3]);
      SbVec2s vpSz = coin_command_viewport_size(cmd, params);
      glUniform2f(this->lineUVpSizeLocation,
                  static_cast<float>(vpSz[0]),
                  static_cast<float>(vpSz[1]));
      glUniform1f(this->lineULineWidthLocation, lw);
      glUniform1f(this->lineUStipplePeriodLocation, 0.0f);
      glUniform3f(this->lineUEmissiveColorLocation, 0.0f, 0.0f, 0.0f);
    } else {
      glLineWidth(lw);
    }
  }

  // Line stipple pattern (dashed/dotted lines) — shader-based via u_stipplePeriod.
  // Compute the stipple period in object-space units by projecting a unit
  // vector through the MVP to get the pixels-per-unit scale at this object.
  uint16_t pattern = cmd.state.raster.linePattern;
  bool useStipple = (pattern != 0 && pattern != 0xFFFF)
                 && (prim == GL_LINES || prim == GL_LINE_STRIP || fillMode == 1);
  if (useStipple) {
    int factor = std::max(static_cast<int>(cmd.state.raster.linePatternScale), 1);
    // Find the fundamental repeat period from the pattern. GL_LINE_STIPPLE
    // uses a 16-bit pattern, but many patterns repeat at a shorter period
    // (e.g. 0x0f0f repeats every 8 bits: 00001111).
    int repeatLen = 16;
    for (int len = 1; len <= 8; len++) {
      if (16 % len != 0) continue;
      uint16_t mask = (1u << len) - 1;
      uint16_t first = pattern & mask;
      bool repeats = true;
      for (int off = len; off < 16; off += len) {
        if (((pattern >> off) & mask) != first) { repeats = false; break; }
      }
      if (repeats) { repeatLen = len; break; }
    }
    float pixelPeriod = static_cast<float>(factor * repeatLen);

    // Project origin and unit X through MVP to get screen scale
    SbMatrix mvp = cmd.modelMatrix;
    mvp.multRight(params.viewMatrix);
    mvp.multRight(params.projMatrix);
    SbVec3f ndc0, ndc1;
    mvp.multVecMatrix(SbVec3f(0, 0, 0), ndc0);
    mvp.multVecMatrix(SbVec3f(1, 0, 0), ndc1);
    SbVec2s vpSz = coin_command_viewport_size(cmd, params);
    float pixPerUnit = (ndc1 - ndc0).length() * vpSz[0] * 0.5f;
    float objectPeriod = (pixPerUnit > 0.001f) ? pixelPeriod / pixPerUnit : 1.0f;

    if (useLineShader) {
      glUniform1f(this->lineUStipplePeriodLocation, objectPeriod);
    } else {
      glUniform1f(this->uStipplePeriodLocation, objectPeriod);
    }
  }

  // Polygon offset: push faces back so coplanar edges render on top
  float oFactor = cmd.state.raster.polygonOffsetFactor;
  float oUnits = cmd.state.raster.polygonOffsetUnits;
  bool useOffset = (prim == GL_TRIANGLES || prim == GL_TRIANGLE_STRIP)
                && fillMode == 0
                && (oFactor != 0.0f || oUnits != 0.0f);
  if (useOffset) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(oFactor, oUnits);
  }

  // Textured commands: set renderMode and bind texture (same shader program)
  bool isTextured = (entry.textureId != 0 && entry.texcoordVBO != 0);
  if (isTextured) {
    bool isBillboard = (cmd.material.flags & SO_MAT_IS_BILLBOARD) != 0;
    bool isPixelText = (cmd.material.flags & SO_MAT_IS_PIXEL_TEXT) != 0;
    // renderMode: 2=billboard, 3=world-space textured, 4=pixel text
    glUniform1f(this->uRenderModeLocation,
                isPixelText ? 4.0f : (isBillboard ? 2.0f : 3.0f));

    if (isBillboard) {
      // Compute quad center from vertex positions (average of all vertices)
      float cx = 0, cy = 0, cz = 0;
      GLsizei stride = static_cast<GLsizei>(
        cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);
      for (uint32_t vi = 0; vi < cmd.geometry.vertexCount; vi++) {
        const float * p = reinterpret_cast<const float *>(
          reinterpret_cast<const char *>(cmd.geometry.positions) + vi * stride);
        cx += p[0]; cy += p[1]; cz += p[2];
      }
      float n = static_cast<float>(cmd.geometry.vertexCount);
      glUniform3f(this->uQuadCenterLocation, cx / n, cy / n, cz / n);

      // Texture pixel size and viewport size
      glUniform2f(this->uTexSizeLocation,
                  static_cast<float>(cmd.material.texture.width),
                  static_cast<float>(cmd.material.texture.height));
      SbVec2s vpSz = coin_command_viewport_size(cmd, params);
      glUniform2f(this->uVpSizeLocation,
                  static_cast<float>(vpSz[0]),
                  static_cast<float>(vpSz[1]));
      if (isPixelText) {
        glUniform2f(this->uPixelTextOriginLocation,
                    static_cast<float>(cmd.pixelText.originX),
                    static_cast<float>(cmd.pixelText.originY));
      }
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, entry.textureId);
    // Sampler state belongs to the draw command, not the cached texture.
    // Reapply it here because one texture cache entry can be reused by
    // commands with different quality or wrap state.
    applyTextureSampler(cmd.material.texture);
    glUniform1i(this->uTextureLocation, 0);
    // Modulate texture with diffuse color (MODULATE mode for NaviCube labels).
    // Billboard textures (SoImage, SoText2) use white modulation (pass-through).
    const SbVec4f & diff = cmd.material.diffuse;
    const float textureAlpha = cmd.material.textureAlphaIncludesOpacity ||
                               (hasVertexColors &&
                                cmd.material.vertexColorAlphaIncludesOpacity)
      ? 1.0f : diff[3];
    if (isBillboard) {
      glUniform4f(this->uTexModColorLocation, 1.0f, 1.0f, 1.0f,
                  textureAlpha);
    } else {
      glUniform4f(this->uTexModColorLocation, diff[0], diff[1], diff[2],
                  textureAlpha);
    }
  }

  glBindVertexArray(entry.vao);

  // --- Draw call ---
  if (cmd.geometry.indexCount > 0) {
    glDrawElements(prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, nullptr);
  }
  else {
    glDrawArrays(prim, 0, cmd.geometry.vertexCount);
  }

  // --- State restore ---
  if (isTextured) {
    glBindTexture(GL_TEXTURE_2D, 0);
    glUniform1f(this->uRenderModeLocation, 0.0f);  // restore to lit mode
  }

  if (useOffset) {
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
  if (useStipple) {
    if (useLineShader) {
      glUniform1f(this->lineUStipplePeriodLocation, 0.0f);
    } else {
      glUniform1f(this->uStipplePeriodLocation, 0.0f);
    }
  }
  if (fillMode != 0 && (prim == GL_TRIANGLES || prim == GL_TRIANGLE_STRIP)) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }
  if (prim == GL_POINTS || fillMode == 2) {
    if (usePointShader) {
      glUseProgram(this->shaderProgram);
    }
    glPointSize(1.0f);
  }
  if (prim == GL_LINES || prim == GL_LINE_STRIP || fillMode == 1) {
    if (useLineShader) {
      glUseProgram(this->shaderProgram);
    }
    glLineWidth(1.0f);
  }
  if (cmd.state.raster.cullMode != 0) {
    glDisable(GL_CULL_FACE);
  }
}

void
SoGLRenderBackend::bindPointShader(const SoRenderCommand & cmd,
                                   const SbMat & viewMat,
                                   const SbMat & projMat,
                                   const SbVec4f & color,
                                   bool useVertexColor,
                                   bool roundPoints,
                                   float pointSize,
                                   const SbVec2s & viewportSize)
{
  glUseProgram(this->pointShaderProgram);

  SbMat modelMat;
  cmd.modelMatrix.getValue(modelMat);
  glUniformMatrix4fv(this->pointUModelLocation, 1, GL_FALSE, &modelMat[0][0]);
  if (cmd.pass == SO_RENDERPASS_OVERLAY) {
    SbMat cmdViewMat, cmdProjMat;
    cmd.viewMatrix.getValue(cmdViewMat);
    cmd.projMatrix.getValue(cmdProjMat);
    glUniformMatrix4fv(this->pointUViewLocation, 1, GL_FALSE, &cmdViewMat[0][0]);
    glUniformMatrix4fv(this->pointUProjLocation, 1, GL_FALSE, &cmdProjMat[0][0]);
  } else {
    glUniformMatrix4fv(this->pointUViewLocation, 1, GL_FALSE, &viewMat[0][0]);
    glUniformMatrix4fv(this->pointUProjLocation, 1, GL_FALSE, &projMat[0][0]);
  }
  glUniform4f(this->pointUColorLocation, color[0], color[1], color[2], color[3]);
  glUniform1f(this->pointUUseVertexColorLocation, useVertexColor ? 1.0f : 0.0f);
  glUniform1f(this->pointURoundPointsLocation, roundPoints ? 1.0f : 0.0f);
  glUniform1f(this->pointUPointSizeLocation, std::max(pointSize, 1.0f));
  glUniform2f(this->pointUVpSizeLocation,
              static_cast<float>(viewportSize[0]),
              static_cast<float>(viewportSize[1]));
}

// -----------------------------------------------------------------------
// Render pass methods
// -----------------------------------------------------------------------

void
SoGLRenderBackend::beginFrame(const SoDrawList & drawlist,
                              const SoRenderParams & params)
{
  this->debugValidateDrawList(drawlist);
  this->logFrameStats(drawlist, params);
  this->currentFrame = params.frameIndex;

  // Only re-render the ID buffer when camera or scene changes
  if (!matricesInitialized ||
      params.viewMatrix != lastViewMatrix ||
      params.projMatrix != lastProjMatrix) {
    this->pickBufferDirty = true;
    lastViewMatrix = params.viewMatrix;
    lastProjMatrix = params.projMatrix;
    matricesInitialized = true;
  }

  // Drain any GL errors left by legacy Coin code (SoGLRenderAction
  // makes deprecated calls that generate errors in Core Profile)
  while (glGetError() != GL_NO_ERROR) {}

  // Clear color buffer when requested (single-color background mode,
  // or first frame). Without this, the framebuffer retains stale content
  // when no gradient background covers the viewport.
  if (params.flags & SO_PARAM_CLEAR_WINDOW) {
    const SbColor4f & cc = params.clearColor;
    glClearColor(cc[0], cc[1], cc[2], cc[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
  else if (params.flags & SO_PARAM_CLEAR_DEPTH) {
    glClear(GL_DEPTH_BUFFER_BIT);
  }

  // Establish default GL state for the frame
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);

#if defined(COIN_GL_COMPATIBILITY)
  if (params.lineSmoothing) {
    glEnable(GL_LINE_SMOOTH);
  }
  else {
    glDisable(GL_LINE_SMOOTH);
  }
  if (params.pointSmoothing) {
    glEnable(GL_POINT_SMOOTH);
  }
  else {
    glDisable(GL_POINT_SMOOTH);
  }
#endif

  glUseProgram(this->shaderProgram);
  coin_apply_default_viewport(params);

  // Upload view and projection matrices (once per frame)
  SbMat viewMat, projMat;
  params.viewMatrix.getValue(viewMat);
  params.projMatrix.getValue(projMat);
  glUniformMatrix4fv(this->uViewLocation, 1, GL_FALSE, &viewMat[0][0]);
  glUniformMatrix4fv(this->uProjLocation, 1, GL_FALSE, &projMat[0][0]);

  // Default: lighting enabled, no stipple
  glUniform1f(this->uRenderModeLocation, 0.0f);
  glUniform1f(this->uStipplePeriodLocation, 0.0f);
  this->uploadLighting(coin_fallback_lighting());

  // Viewport size for line stipple derivatives and billboard sizing
  SbVec2s vpSz = params.viewport.getViewportSizePixels();
  glUniform2f(this->uVpSizeLocation,
              static_cast<float>(vpSz[0]),
              static_cast<float>(vpSz[1]));
}

void
SoGLRenderBackend::uploadLighting(const SoLightingData & lighting)
{
  const SbVec3f & ambient = lighting.ambient;
  glUniform3f(this->uAmbientLightLocation, ambient[0], ambient[1], ambient[2]);

  GLint lightTypes[MAX_SHADER_LIGHTS] = {0};
  GLfloat lightColors[MAX_SHADER_LIGHTS * 3] = {0.0f};
  GLfloat lightDirections[MAX_SHADER_LIGHTS * 3] = {0.0f};
  GLfloat lightPositions[MAX_SHADER_LIGHTS * 3] = {0.0f};
  GLfloat lightAttenuations[MAX_SHADER_LIGHTS * 3] = {0.0f};
  GLfloat lightSpotParams[MAX_SHADER_LIGHTS * 2] = {0.0f};

  const int lightCount =
    std::min<int>(static_cast<int>(lighting.lights.size()), MAX_SHADER_LIGHTS);
  for (int i = 0; i < lightCount; ++i) {
    const SoLightData & light = lighting.lights[static_cast<size_t>(i)];
    lightTypes[i] = static_cast<GLint>(light.type);
    lightColors[i * 3 + 0] = light.color[0];
    lightColors[i * 3 + 1] = light.color[1];
    lightColors[i * 3 + 2] = light.color[2];
    lightDirections[i * 3 + 0] = light.direction[0];
    lightDirections[i * 3 + 1] = light.direction[1];
    lightDirections[i * 3 + 2] = light.direction[2];
    lightPositions[i * 3 + 0] = light.position[0];
    lightPositions[i * 3 + 1] = light.position[1];
    lightPositions[i * 3 + 2] = light.position[2];
    lightAttenuations[i * 3 + 0] = light.attenuation[0];
    lightAttenuations[i * 3 + 1] = light.attenuation[1];
    lightAttenuations[i * 3 + 2] = light.attenuation[2];
    lightSpotParams[i * 2 + 0] = light.spotCutoffCos;
    lightSpotParams[i * 2 + 1] = light.spotExponent;
  }

  glUniform1i(this->uLightCountLocation, lightCount);
  glUniform1iv(this->uLightTypeLocation, MAX_SHADER_LIGHTS, lightTypes);
  glUniform3fv(this->uLightColorLocation, MAX_SHADER_LIGHTS, lightColors);
  glUniform3fv(this->uLightDirectionLocation, MAX_SHADER_LIGHTS, lightDirections);
  glUniform3fv(this->uLightPositionLocation, MAX_SHADER_LIGHTS, lightPositions);
  glUniform3fv(this->uLightAttenuationLocation, MAX_SHADER_LIGHTS, lightAttenuations);
  glUniform2fv(this->uLightSpotParamsLocation, MAX_SHADER_LIGHTS, lightSpotParams);
}

void
SoGLRenderBackend::applyLighting(const SoDrawList & drawlist,
                                 const SoRenderCommand & cmd)
{
  const SoLightingData * lighting = drawlist.getLighting(cmd.lightingHandle);
  if (!lighting) {
    lighting = &coin_fallback_lighting();
  }
  this->uploadLighting(*lighting);
}

void
SoGLRenderBackend::updateGeometryCache(const SoDrawList & drawlist)
{
  ZoneScopedN("cacheUpdate");
  const int count = drawlist.getNumCommands();
  for (int i = 0; i < count; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommand(i);
    if (cmd.geometry.vertexCount == 0 || !cmd.geometry.positions) continue;
    if (cmd.geometry.vertexCount > MAX_VERTEX_COUNT) continue;

    GLsizei stride = static_cast<GLsizei>(
      cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);

    CachedGPUCommand & entry = getOrCreateCache(cmd.geometry.positions, cmd.geometry.indices);
    uint32_t gen = drawlist.getGeneration();
    bool needsUpload = !entry.isGeometryValid(
      cmd.geometry.positions, cmd.geometry.normals,
      cmd.geometry.indices, cmd.geometry.vertexCount,
      cmd.geometry.indexCount, static_cast<uint32_t>(stride), gen);
    // Also upload if command has texture but cache doesn't
    if (!needsUpload && cmd.material.texture.pixels && entry.textureId == 0) {
      needsUpload = true;
    }
    if (needsUpload) {
      uploadGeometry(entry, cmd);
      setupVisualVAO(entry, cmd);
      entry.cacheGeneration = gen;
    }
    else if (cmd.material.texture.pixels && entry.textureId != 0
             && textureFilterUsesMipmaps(cmd.material.texture.minFilter)
             && !entry.textureHasMipmaps) {
      // The geometry cache can outlive a command's sampler state. Create
      // the mip levels here if a later command needs them; drawCommand()
      // deliberately keeps its cache reference read-only.
      glBindTexture(GL_TEXTURE_2D, entry.textureId);
      glGenerateMipmap(GL_TEXTURE_2D);
      entry.textureHasMipmaps = true;
      glBindTexture(GL_TEXTURE_2D, 0);
    }
    entry.lastUsedFrame = this->currentFrame;
  }
}

void
SoGLRenderBackend::renderBackgroundPass(const SoDrawList & drawlist,
                                        const SbMat & viewMat,
                                        const SbMat & projMat,
                                        const SoRenderParams & params)
{
  int bgCount = params.bgCommandCount;
  int count = drawlist.getNumCommands();
  if (bgCount <= 0) return;

  // Background commands have their own view/proj matrices captured
  // per-command (identity for gradients, ortho for grids, etc.).
  glDepthMask(GL_FALSE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  for (int i = 0; i < bgCount && i < count; ++i) {
    // Background rendering is a pass-level exception: it must remain
    // depth/blend independent even if the captured traversal state came from
    // a surrounding node with different values.
    SoRenderCommand backgroundCommand = drawlist.getCommand(i);
    backgroundCommand.state.depth.enabled = false;
    backgroundCommand.state.depth.writeEnabled = false;
    backgroundCommand.state.blend.enabled = false;
    drawCommand(drawlist, backgroundCommand, viewMat, projMat, params);
  }

  // Restore default state; clear depth so main scene renders on top
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glClear(GL_DEPTH_BUFFER_BIT);
}

void
SoGLRenderBackend::renderOpaquePass(const SoDrawList & drawlist,
                                    SoRenderStage stage,
                                    const SbMat & viewMat,
                                    const SbMat & projMat,
                                    const SoRenderParams & params)
{
  const int count = drawlist.getNumCommands();
  int bgCount = params.bgCommandCount;
  const auto & order = drawlist.getSortedOrder();

  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);

  for (int si = 0; si < count; ++si) {
    int ci = (si < static_cast<int>(order.size())) ? order[si] : si;
    if (ci < bgCount) continue;
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    if (cmd.stage != stage) continue;
    if (cmd.pass != SO_RENDERPASS_OPAQUE) continue;
    drawCommand(drawlist, cmd, viewMat, projMat, params);
  }
}

void
SoGLRenderBackend::renderTransparentPass(const SoDrawList & drawlist,
                                         SoRenderStage stage,
                                         const SbMat & viewMat,
                                         const SbMat & projMat,
                                         const SoRenderParams & params)
{
  const int count = drawlist.getNumCommands();
  int bgCount = params.bgCommandCount;
  const auto & order = drawlist.getSortedOrder();

  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  for (int si = 0; si < count; ++si) {
    int ci = (si < static_cast<int>(order.size())) ? order[si] : si;
    if (ci < bgCount) continue;
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    if (cmd.stage != stage) continue;
    if (cmd.pass != SO_RENDERPASS_TRANSPARENT) continue;
    drawCommand(drawlist, cmd, viewMat, projMat, params);
  }

  // Restore default state
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void
SoGLRenderBackend::clearAfterMainDepth(const SoDrawList & drawlist)
{
  const int count = drawlist.getNumCommands();

  for (int i = 0; i < count; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommand(i);
    if (cmd.stage != SoRenderStage::AfterMain) {
      continue;
    }
    if (cmd.state.raster.clearDepth) {
      glEnable(GL_DEPTH_TEST);
      glDepthMask(GL_TRUE);
      glClear(GL_DEPTH_BUFFER_BIT);
    }
    return;
  }
}

void
SoGLRenderBackend::renderOverlayPass(const SoDrawList & drawlist,
                                     const SbMat & viewMat,
                                     const SbMat & projMat,
                                     const SoRenderParams & params)
{
  const int count = drawlist.getNumCommands();
  int bgCount = params.bgCommandCount;
  const auto & order = drawlist.getSortedOrder();

  // Collect overlay commands, partitioned into 3D (own camera, e.g. NaviCube)
  // and 2D (annotations, constraint labels — use main camera).
  SbMatrix mainView = params.viewMatrix;
  std::vector<int> overlay3D, overlay2D;
  for (int si = 0; si < count; ++si) {
    int ci = (si < static_cast<int>(order.size())) ? order[si] : si;
    if (ci < bgCount) continue;
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    if (cmd.stage != SoRenderStage::Foreground) continue;
    if (cmd.pass != SO_RENDERPASS_OVERLAY) continue;
    // 3D overlays have their own camera (viewMatrix differs from main scene)
    if (cmd.viewMatrix != mainView) {
      overlay3D.push_back(ci);
    } else {
      overlay2D.push_back(ci);
    }
  }

  // 3D overlays (NaviCube): clear depth, enable depth test for self-occlusion
  if (!overlay3D.empty()) {
    coin_clear_overlay_depth(drawlist.getCommand(overlay3D.front()), params);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int ci : overlay3D) {
      const SoRenderCommand & cmd = drawlist.getCommand(ci);
      if (cmd.state.depth.enabled) {
        glEnable(GL_DEPTH_TEST);
      } else {
        glDisable(GL_DEPTH_TEST);
      }
      glDepthFunc(depthFunctionToGL(cmd.state.depth.func));
      glDepthMask(cmd.state.depth.writeEnabled ? GL_TRUE : GL_FALSE);
      drawCommand(drawlist, cmd, viewMat, projMat, params);
    }
    coin_apply_default_viewport(params);
  }

  // 2D overlays (annotations): depth disabled, render on top of everything
  if (!overlay2D.empty()) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int ci : overlay2D) {
      // 2D foreground annotations are an explicit pass-level exception:
      // keep them on top even when their traversal state inherited depth
      // testing from the surrounding scene.
      SoRenderCommand annotationCommand = drawlist.getCommand(ci);
      annotationCommand.state.depth.enabled = false;
      annotationCommand.state.depth.writeEnabled = false;
      drawCommand(drawlist, annotationCommand, viewMat, projMat, params);
    }
    coin_apply_default_viewport(params);
  }

  // Restore default state
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void
SoGLRenderBackend::renderSelectionPass(const SoDrawList & drawlist,
                                       SoRenderStage stage,
                                       const SbMat & viewMat,
                                       const SbMat & projMat,
                                       const SoRenderParams & params)
{
  const int count = drawlist.getNumCommands();

  // Selection/highlight overlays are depth-tested, but must not change depth.
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUniform1f(this->uRenderModeLocation, 1.0f);

  auto drawWholeCommand = [](const SoRenderCommand & cmd, GLenum prim) {
    if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
      glDrawElements(prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    else {
      glDrawArrays(prim, 0, cmd.geometry.vertexCount);
    }
  };

  auto drawRange = [](const SoRenderCommand & cmd,
                      const SoRenderElementRange & range,
                      GLenum prim) {
    if (range.drawCount <= 0) return;
    if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
      glDrawElements(prim, range.drawCount, GL_UNSIGNED_INT,
                     reinterpret_cast<const void *>(
                       static_cast<uintptr_t>(range.drawStart * sizeof(uint32_t))));
    }
    else {
      glDrawArrays(prim, range.drawStart, range.drawCount);
    }
  };

  auto drawElementRanges = [&drawRange](const SoRenderCommand & cmd,
                                        int elementIndex,
                                        GLenum prim) {
    bool drew = false;
    for (const SoRenderElementRange & range : cmd.pick.elementRanges) {
      if (range.elementIndex == elementIndex) {
        drawRange(cmd, range, prim);
        drew = true;
      }
    }
    return drew;
  };

  for (int i = 0; i < count; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommand(i);
    if (cmd.stage != stage) continue;
    bool hasHighlight = cmd.selection.highlightWholeObject
                     || !cmd.selection.highlightedElements.empty();
    bool hasSelection = cmd.selection.selectWholeObject || !cmd.selection.selectedElements.empty();
    if (!hasHighlight && !hasSelection) continue;

    if (!cmd.geometry.positions) continue;
    auto it = ptrToCacheIndex.find(CacheKey{cmd.geometry.positions, cmd.geometry.indices});
    if (it == ptrToCacheIndex.end()) continue;
    const CachedGPUCommand & entry = gpuCache[it->second];
    if (entry.vao == 0) continue;

    SbMat modelMat;
    cmd.modelMatrix.getValue(modelMat);
    glUniformMatrix4fv(this->uModelLocation, 1, GL_FALSE, &modelMat[0][0]);
    glUniformMatrix4fv(this->uViewLocation, 1, GL_FALSE, &viewMat[0][0]);
    glUniformMatrix4fv(this->uProjLocation, 1, GL_FALSE, &projMat[0][0]);

    GLenum prim = topologyToGL(cmd.geometry.topology);
    bool pointShaderActive = false;
    bool lineShaderActive = false;

    if (prim == GL_POINTS) {
      float pointSize = cmd.state.raster.pointSize;
      if (pointSize < 1.0f) pointSize = cmd.state.raster.lineWidth;
      pointSize = std::max(pointSize, MIN_SELECTION_POINT_SIZE) * params.devicePixelRatio;
      if (this->pointShaderProgram) {
        pointShaderActive = true;
        this->bindPointShader(cmd,
                              viewMat,
                              projMat,
                              SbVec4f(1.0f, 1.0f, 1.0f, 1.0f),
                              false,
                              false,
                              pointSize,
                              coin_command_viewport_size(cmd, params));
      } else {
        glPointSize(pointSize);
        glUniform1f(this->uRenderModeLocation, 1.0f);
      }
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    if (prim == GL_LINES || prim == GL_LINE_STRIP) {
      // Render edge highlight at same width as original edge, opaque,
      // using GL_ALWAYS to overwrite the black edge with highlight color.
      // This matches the legacy renderer which replaces the edge color.
      glDepthFunc(GL_ALWAYS);
      float edgeWidth = std::max(cmd.state.raster.lineWidth, 1.0f) * params.devicePixelRatio;
      if (this->shouldUseWideLineShader(edgeWidth)) {
        lineShaderActive = true;
        glUseProgram(this->lineShaderProgram);
        glUniformMatrix4fv(this->lineUModelLocation, 1, GL_FALSE, &modelMat[0][0]);
        glUniformMatrix4fv(this->lineUViewLocation, 1, GL_FALSE, &viewMat[0][0]);
        glUniformMatrix4fv(this->lineUProjLocation, 1, GL_FALSE, &projMat[0][0]);
        SbVec2s vpSz = params.viewport.getViewportSizePixels();
        glUniform2f(this->lineUVpSizeLocation,
                    static_cast<float>(vpSz[0]), static_cast<float>(vpSz[1]));
        glUniform1f(this->lineULineWidthLocation, edgeWidth);
        glUniform1f(this->lineUStipplePeriodLocation, 0.0f);
        glUniform1f(this->lineUUseVertexColorLocation, 0.0f);
      } else {
        glLineWidth(edgeWidth);
      }
    }

    // Bind cached VAO (has pos + norm + idx already set up).
    // For flat overlay, normals are ignored by the shader (u_renderMode == 1).
    glBindVertexArray(entry.vao);

    // Helper: set color on whichever shader is active (main or line).
    // For edges, use opaque color (replaces the edge, not overlays).
    bool isEdge = (prim == GL_LINES || prim == GL_LINE_STRIP);
    bool isPoint = (prim == GL_POINTS);
    auto setSelColor = [&](float r, float g, float b, float a) {
      float alpha = (isEdge || isPoint) ? 1.0f : a;  // opaque for edges/points
      if (lineShaderActive) {
        glUniform4f(this->lineUColorLocation, r, g, b, alpha);
      } else if (pointShaderActive) {
        glUniform4f(this->pointUColorLocation, r, g, b, alpha);
      } else {
        glUniform4f(this->uColorLocation, r, g, b, alpha);
      }
    };

    if (hasSelection) {
      const SbVec4f & sc = cmd.selection.selectionColor;
      setSelColor(sc[0], sc[1], sc[2], SELECTION_ALPHA);

      // Render wireframe bounding box only for whole-object selection from
      // tree-view selection. Per-element click selection uses the normal
      // element range overlay path below.
      if (cmd.selection.selectWholeObject && prim == GL_TRIANGLES
          && cmd.geometry.positions && cmd.geometry.vertexCount >= 3) {
        // Compute AABB from vertex positions
        GLsizei stride = static_cast<GLsizei>(
          cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);
        int floatStride = stride / static_cast<int>(sizeof(float));
        const float * pos = cmd.geometry.positions;
        float minX = pos[0], minY = pos[1], minZ = pos[2];
        float maxX = minX, maxY = minY, maxZ = minZ;
        for (uint32_t vi = 1; vi < cmd.geometry.vertexCount; vi++) {
          const float * p = pos + vi * floatStride;
          if (p[0] < minX) minX = p[0]; if (p[0] > maxX) maxX = p[0];
          if (p[1] < minY) minY = p[1]; if (p[1] > maxY) maxY = p[1];
          if (p[2] < minZ) minZ = p[2]; if (p[2] > maxZ) maxZ = p[2];
        }
        // 24 vertices for 12 edges of a wireframe box (GL_LINES)
        float bboxVerts[24 * 3] = {
          minX,minY,minZ, maxX,minY,minZ,  maxX,minY,minZ, maxX,maxY,minZ,
          maxX,maxY,minZ, minX,maxY,minZ,  minX,maxY,minZ, minX,minY,minZ,
          minX,minY,maxZ, maxX,minY,maxZ,  maxX,minY,maxZ, maxX,maxY,maxZ,
          maxX,maxY,maxZ, minX,maxY,maxZ,  minX,maxY,maxZ, minX,minY,maxZ,
          minX,minY,minZ, minX,minY,maxZ,  maxX,minY,minZ, maxX,minY,maxZ,
          maxX,maxY,minZ, maxX,maxY,maxZ,  minX,maxY,minZ, minX,maxY,maxZ,
        };
        GLuint bboxVBO;
        glGenBuffers(1, &bboxVBO);
        glBindBuffer(GL_ARRAY_BUFFER, bboxVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(bboxVerts), bboxVerts, GL_STREAM_DRAW);

        GLuint bboxVAO;
        glGenVertexArrays(1, &bboxVAO);
        glBindVertexArray(bboxVAO);
        glEnableVertexAttribArray(this->posLoc);
        glVertexAttribPointer(this->posLoc, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        if (this->normLoc >= 0) {
          glDisableVertexAttribArray(this->normLoc);
          glVertexAttrib3f(this->normLoc, 0.0f, 0.0f, 1.0f);
        }
        if (this->colorLoc >= 0) {
          glDisableVertexAttribArray(this->colorLoc);
        }
        glUniform1f(this->uUseVertexColorLocation, 0.0f);
        glUniform4f(this->uColorLocation, sc[0], sc[1], sc[2], 1.0f);
        glLineWidth(2.0f * params.devicePixelRatio);
        glDrawArrays(GL_LINES, 0, 24);
        glLineWidth(1.0f);
        glBindVertexArray(0);
        glDeleteVertexArrays(1, &bboxVAO);
        glDeleteBuffers(1, &bboxVBO);
        // Rebind the original VAO for subsequent draws
        glBindVertexArray(entry.vao);
      }
      else {
        if (cmd.selection.selectWholeObject) {
          drawWholeCommand(cmd, prim);
        }
        for (int elem : cmd.selection.selectedElements) {
          drawElementRanges(cmd, elem, prim);
        }
      }
    }

    if (hasHighlight) {
      const SbVec4f & hc = cmd.selection.highlightColor;
      const float highlightAlpha = std::max(0.0f, std::min(hc[3], 1.0f));
      setSelColor(hc[0], hc[1], hc[2], highlightAlpha);
      if (prim == GL_POINTS) {
        // Match the legacy point overlay path: committed selection respects
        // depth, while the live highlight renders on top.
        glDepthFunc(GL_ALWAYS);
      }
      if (cmd.selection.highlightWholeObject) {
        drawWholeCommand(cmd, prim);
      }
      else {
        for (int elem : cmd.selection.highlightedElements) {
          drawElementRanges(cmd, elem, prim);
        }
      }
    }

    if (prim == GL_POINTS) {
      if (pointShaderActive) {
        glUseProgram(this->shaderProgram);
      }
      glUniform1f(this->uRenderModeLocation, 1.0f);  // restore to flat
      glDepthFunc(GL_LEQUAL);
    }
    if (prim == GL_LINES || prim == GL_LINE_STRIP) {
      glDepthFunc(GL_LEQUAL);
      if (lineShaderActive) {
        glUseProgram(this->shaderProgram);
        glUniform1f(this->uRenderModeLocation, 1.0f);
      } else {
        glLineWidth(1.0f);
      }
    }
  }

  // Restore default state
  glUniform1f(this->uRenderModeLocation, 0.0f);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void
SoGLRenderBackend::endFrame()
{
#if defined(__APPLE__) && !defined(glBindVertexArray)
  glBindVertexArrayAPPLE(0);
#else
  glBindVertexArray(0);
#endif
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glUseProgram(0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_POLYGON_OFFSET_FILL);
  gcStaleEntries(this->currentFrame);
}

void
SoGLRenderBackend::renderIDBufferPass(const SoDrawList & drawlist,
                                      const SbMat & viewMat,
                                      const SbMat & projMat,
                                      const SoRenderParams & params)
{
  // Skip during interactive navigation (no preselection during orbit/pan/zoom)
  bool interactive = (params.flags & SO_PARAM_INTERACTIVE) != 0;
  bool skipIdBuffer = (params.flags & SO_PARAM_SKIP_ID) != 0;
  if (!pickBuffer || interactive || skipIdBuffer) return;

  const int count = drawlist.getNumCommands();
  const auto & lut = drawlist.getPickLUT();
  SbVec2s vpSize = params.viewport.getViewportSizePixels();

  // Render ID buffer at half resolution — 4x less fragment work.
  // Pick radius and line/point sizes still provide adequate coverage.
  int idW = std::max(1, static_cast<int>(vpSize[0]) / 2);
  int idH = std::max(1, static_cast<int>(vpSize[1]) / 2);
  pickBuffer->resize(idW, idH);
  pickBuffer->setPickScale(static_cast<float>(idW) / vpSize[0],
                           static_cast<float>(idH) / vpSize[1]);

  if (lut.size() != lastPickLUTSize) {
    pickBuffer->buildIdColorVBOs(drawlist, params.contextId);
    lastPickLUTSize = lut.size();
    pickBufferDirty = true;
  }

  if (pickBufferDirty && !lut.empty()) {
    // Build per-command VBO info array so the ID pass can reuse cached VBOs
    std::vector<SoIDPassVBOInfo> vboInfo(count);
    for (int i = 0; i < count; ++i) {
      const SoRenderCommand & cmd = drawlist.getCommand(i);
      vboInfo[i] = {0, 0, 0};
      if (!cmd.geometry.positions) continue;
      auto it = ptrToCacheIndex.find(
        CacheKey{cmd.geometry.positions, cmd.geometry.indices});
      if (it != ptrToCacheIndex.end()) {
        const CachedGPUCommand & entry = gpuCache[it->second];
        vboInfo[i].posVBO = entry.posVBO;
        vboInfo[i].idxVBO = entry.idxVBO;
        vboInfo[i].vertexStride = entry.vertexStride;
      }
    }
    pickBuffer->render(&viewMat[0][0], &projMat[0][0], drawlist,
                       vboInfo.data(), count);
    pickBufferDirty = false;
  }

  static int showIdBuffer = -1;
  if (showIdBuffer < 0) {
    const char * env = coin_getenv("FREECAD_SHOW_ID_BUFFER");
    showIdBuffer = (env && env[0] == '1') ? 1 : 0;
  }
  if (showIdBuffer) {
    pickBuffer->blitToScreen(vpSize[0], vpSize[1]);
  }
}

// -----------------------------------------------------------------------
// Render — orchestrator
// -----------------------------------------------------------------------

SbBool
SoGLRenderBackend::render(const SoDrawList & drawlist,
                          const SoRenderParams & params)
{
  ZoneScopedN("GLBackend::render");
  if (!this->shaderProgram) return TRUE;

  SbMat viewMat, projMat;
  params.viewMatrix.getValue(viewMat);
  params.projMatrix.getValue(projMat);

  beginFrame(drawlist, params);
  updateGeometryCache(drawlist);
  renderBackgroundPass(drawlist, viewMat, projMat, params);
  renderOpaquePass(drawlist, SoRenderStage::Main, viewMat, projMat, params);
  renderTransparentPass(drawlist, SoRenderStage::Main, viewMat, projMat, params);
  renderSelectionPass(drawlist, SoRenderStage::Main, viewMat, projMat, params);
  clearAfterMainDepth(drawlist);
  renderOpaquePass(drawlist, SoRenderStage::AfterMain, viewMat, projMat, params);
  renderTransparentPass(drawlist, SoRenderStage::AfterMain, viewMat, projMat, params);
  renderSelectionPass(drawlist, SoRenderStage::AfterMain, viewMat, projMat, params);
  renderOverlayPass(drawlist, viewMat, projMat, params);
  renderIDBufferPass(drawlist, viewMat, projMat, params);
  endFrame();

  return TRUE;
}

// -----------------------------------------------------------------------
// Misc
// -----------------------------------------------------------------------

void
SoGLRenderBackend::resizeTarget(const SoRenderTargetInfo & info)
{
  this->storedparams.targetInfo = info;
  this->pickBufferDirty = true;
  SoRenderBackend::resizeTarget(info);
}

uint32_t
SoGLRenderBackend::pick(int x, int y, int pickRadius) const
{
  if (!pickBuffer) return 0;
  return pickBuffer->pick(x, y, pickRadius);
}

void
SoGLRenderBackend::logFrameStats(const SoDrawList & drawlist,
                                 const SoRenderParams & params) const
{
  if (!coin_render_ir_trace_enabled()) {
    return;
  }

  const int num = drawlist.getNumCommands();
  SoDebugError::postInfo("SoGLRenderBackend::render",
                         "frame=%d cmds=%d",
                         params.frameIndex, num);
  SoIRDumpSummary(drawlist);

  SoIRDumpFirstN(drawlist, 8);
}

bool
SoGLRenderBackend::createShaders()
{
  // Generated from data/shaders/backend/*.glsl.
  static const char * vertexSource = BACKENDUNIFIEDVERTEX_shadersource;
  static const char * fragmentSource = BACKENDUNIFIEDFRAGMENT_shadersource;

  GLuint vs = coin_compile_shader(GL_VERTEX_SHADER, vertexSource);
  GLuint fs = coin_compile_shader(GL_FRAGMENT_SHADER, fragmentSource);
  if (vs == 0 || fs == 0) {
    glDeleteShader(vs);
    glDeleteShader(fs);
    return FALSE;
  }

  // Bind attribute locations explicitly before linking — the macOS GLSL 1.20
  // compiler may reassign locations when a new attribute (a_texcoord) is added,
  // and we need stable locations that match the VAO setup.
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glBindAttribLocation(prog, 0, "a_position");
  glBindAttribLocation(prog, 1, "a_normal");
  glBindAttribLocation(prog, 2, "a_color");
  glBindAttribLocation(prog, 3, "a_texcoord");
  glBindAttribLocation(prog, 4, "a_lineDistance");
  glLinkProgram(prog);
  GLint linkStatus = GL_FALSE;
  glGetProgramiv(prog, GL_LINK_STATUS, &linkStatus);
  if (linkStatus == GL_FALSE) {
    GLint length = 0;
    glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &length);
    if (length > 0) {
      std::string log(length, '\0');
      glGetProgramInfoLog(prog, length, &length, &log[0]);
      SoDebugError::postInfo("SoGLRenderBackend::linkProgram", "%s", log.c_str());
    }
    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return FALSE;
  }
  this->shaderProgram = prog;
  glDeleteShader(vs);
  glDeleteShader(fs);

  this->uViewLocation = glGetUniformLocation(this->shaderProgram, "u_view");
  this->uProjLocation = glGetUniformLocation(this->shaderProgram, "u_proj");
  this->uModelLocation = glGetUniformLocation(this->shaderProgram, "u_model");
  this->uColorLocation = glGetUniformLocation(this->shaderProgram, "u_color");
  this->uRenderModeLocation = glGetUniformLocation(this->shaderProgram, "u_renderMode");
  this->uShadingModelLocation = glGetUniformLocation(this->shaderProgram, "u_shadingModel");
  this->uEmissiveColorLocation = glGetUniformLocation(this->shaderProgram, "u_emissiveColor");
  this->uUseVertexColorLocation = glGetUniformLocation(this->shaderProgram, "u_useVertexColor");
  this->uTextureLocation = glGetUniformLocation(this->shaderProgram, "u_texture");
  this->uTexModColorLocation = glGetUniformLocation(this->shaderProgram, "u_texModColor");
  this->uAlphaTestPolicyLocation = glGetUniformLocation(this->shaderProgram, "u_alphaTestPolicy");
  this->uAlphaTestFunctionLocation = glGetUniformLocation(this->shaderProgram, "u_alphaTestFunction");
  this->uAlphaTestReferenceLocation = glGetUniformLocation(this->shaderProgram, "u_alphaTestReference");
  this->uVertexColorAlphaIncludesOpacityLocation =
    glGetUniformLocation(this->shaderProgram, "u_vertexColorAlphaIncludesOpacity");
  this->uQuadCenterLocation = glGetUniformLocation(this->shaderProgram, "u_quadCenter");
  this->uTexSizeLocation = glGetUniformLocation(this->shaderProgram, "u_texSize");
  this->uVpSizeLocation = glGetUniformLocation(this->shaderProgram, "u_vpSize");
  this->uPixelTextOriginLocation = glGetUniformLocation(this->shaderProgram, "u_pixelTextOrigin");
  this->uStipplePeriodLocation = glGetUniformLocation(this->shaderProgram, "u_stipplePeriod");
  this->uAmbientLightLocation = glGetUniformLocation(this->shaderProgram, "u_ambientLight");
  this->uLightCountLocation = glGetUniformLocation(this->shaderProgram, "u_lightCount");
  this->uLightTypeLocation = glGetUniformLocation(this->shaderProgram, "u_lightType[0]");
  this->uLightColorLocation = glGetUniformLocation(this->shaderProgram, "u_lightColor[0]");
  this->uLightDirectionLocation = glGetUniformLocation(this->shaderProgram, "u_lightDirection[0]");
  this->uLightPositionLocation = glGetUniformLocation(this->shaderProgram, "u_lightPosition[0]");
  this->uLightAttenuationLocation = glGetUniformLocation(this->shaderProgram, "u_lightAttenuation[0]");
  this->uLightSpotParamsLocation = glGetUniformLocation(this->shaderProgram, "u_lightSpotParams[0]");
  this->uMaterialAmbientLocation = glGetUniformLocation(this->shaderProgram, "u_materialAmbient");
  this->uMaterialSpecularLocation = glGetUniformLocation(this->shaderProgram, "u_materialSpecular");
  this->uMaterialShininessLocation = glGetUniformLocation(this->shaderProgram, "u_materialShininess");
  this->texcoordLoc = glGetAttribLocation(this->shaderProgram, "a_texcoord");
  this->lineDistLoc = glGetAttribLocation(this->shaderProgram, "a_lineDistance");

  // Line shader — geometry shader expands lines into screen-space quads
  // for wide lines on Core Profile where glLineWidth is clamped to 1.0.
  static const char * lineVertSource = BACKENDWIDELINEVERTEX_shadersource;
  static const char * lineGeomSource = BACKENDWIDELINEGEOMETRY_shadersource;
  static const char * lineFragSource = BACKENDWIDELINEFRAGMENT_shadersource;

  GLuint lvs = coin_compile_shader(GL_VERTEX_SHADER, lineVertSource);
  GLuint lgs = coin_compile_shader(GL_GEOMETRY_SHADER, lineGeomSource);
  GLuint lfs = coin_compile_shader(GL_FRAGMENT_SHADER, lineFragSource);
  if (lvs && lgs && lfs) {
    GLuint lprog = glCreateProgram();
    glAttachShader(lprog, lvs);
    glAttachShader(lprog, lgs);
    glAttachShader(lprog, lfs);
    glBindAttribLocation(lprog, 0, "a_position");
    glBindAttribLocation(lprog, 2, "a_color");
    glBindAttribLocation(lprog, 4, "a_lineDistance");
    glLinkProgram(lprog);
    GLint linkOk = GL_FALSE;
    glGetProgramiv(lprog, GL_LINK_STATUS, &linkOk);
    if (linkOk) {
      this->lineShaderProgram = lprog;
      this->lineUViewLocation = glGetUniformLocation(lprog, "u_view");
      this->lineUProjLocation = glGetUniformLocation(lprog, "u_proj");
      this->lineUModelLocation = glGetUniformLocation(lprog, "u_model");
      this->lineUColorLocation = glGetUniformLocation(lprog, "u_color");
      this->lineULineWidthLocation = glGetUniformLocation(lprog, "u_lineWidth");
      this->lineUVpSizeLocation = glGetUniformLocation(lprog, "u_vpSize");
      this->lineURenderModeLocation = glGetUniformLocation(lprog, "u_renderMode");
      this->lineUStipplePeriodLocation = glGetUniformLocation(lprog, "u_stipplePeriod");
      this->lineUEmissiveColorLocation = glGetUniformLocation(lprog, "u_emissiveColor");
      this->lineUUseVertexColorLocation = glGetUniformLocation(lprog, "u_useVertexColor");
    } else {
      glDeleteProgram(lprog);
    }
  }
  glDeleteShader(lvs);
  glDeleteShader(lgs);
  glDeleteShader(lfs);

  // Point shader — geometry shader expands points into screen-space quads
  // for stable marker sizing and circular vertex overlays.
  static const char * pointVertSource = BACKENDWIDEPOINTVERTEX_shadersource;
  static const char * pointGeomSource = BACKENDWIDEPOINTGEOMETRY_shadersource;
  static const char * pointFragSource = BACKENDWIDEPOINTFRAGMENT_shadersource;

  GLuint pvs = coin_compile_shader(GL_VERTEX_SHADER, pointVertSource);
  GLuint pgs = coin_compile_shader(GL_GEOMETRY_SHADER, pointGeomSource);
  GLuint pfs = coin_compile_shader(GL_FRAGMENT_SHADER, pointFragSource);
  if (pvs && pgs && pfs) {
    GLuint pprog = glCreateProgram();
    glAttachShader(pprog, pvs);
    glAttachShader(pprog, pgs);
    glAttachShader(pprog, pfs);
    glBindAttribLocation(pprog, 0, "a_position");
    glBindAttribLocation(pprog, 2, "a_color");
    glLinkProgram(pprog);
    GLint linkOk = GL_FALSE;
    glGetProgramiv(pprog, GL_LINK_STATUS, &linkOk);
    if (linkOk) {
      this->pointShaderProgram = pprog;
      this->pointUViewLocation = glGetUniformLocation(pprog, "u_view");
      this->pointUProjLocation = glGetUniformLocation(pprog, "u_proj");
      this->pointUModelLocation = glGetUniformLocation(pprog, "u_model");
      this->pointUColorLocation = glGetUniformLocation(pprog, "u_color");
      this->pointUPointSizeLocation = glGetUniformLocation(pprog, "u_pointSize");
      this->pointURoundPointsLocation = glGetUniformLocation(pprog, "u_roundPoints");
      this->pointUVpSizeLocation = glGetUniformLocation(pprog, "u_vpSize");
      this->pointUUseVertexColorLocation = glGetUniformLocation(pprog, "u_useVertexColor");
    } else {
      glDeleteProgram(pprog);
    }
  }
  glDeleteShader(pvs);
  glDeleteShader(pgs);
  glDeleteShader(pfs);

  return TRUE;
}

void
SoGLRenderBackend::setPickLineWidth(float width)
{
  if (pickBuffer) pickBuffer->setPickLineWidth(width);
}

void
SoGLRenderBackend::setPickPointSize(float size)
{
  if (pickBuffer) pickBuffer->setPickPointSize(size);
}

float
SoGLRenderBackend::getPickLineWidth() const
{
  return pickBuffer ? pickBuffer->getPickLineWidth() : DEFAULT_PICK_SIZE;
}

float
SoGLRenderBackend::getPickPointSize() const
{
  return pickBuffer ? pickBuffer->getPickPointSize() : DEFAULT_PICK_SIZE;
}
