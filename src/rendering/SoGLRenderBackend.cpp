// src/rendering/SoGLRenderBackend.cpp

#include "rendering/SoGLRenderBackend.h"

// GL_GEOMETRY_SHADER may not be defined in macOS legacy GL headers
#ifndef GL_GEOMETRY_SHADER
#define GL_GEOMETRY_SHADER 0x8DD9
#endif

#include <Inventor/SbBasic.h>
#include <Inventor/C/tidbits.h>
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/SbMatrix.h>

#include "glue/glp.h"
#include "glue/glslp.h"

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

#include "shaders/SoGLShaderProgram.h"

// -----------------------------------------------------------------------
// Named constants — extracted from magic numbers throughout render()
// -----------------------------------------------------------------------

// Blinn-Phong lighting coefficients (used in GLSL shader source)
static constexpr float AMBIENT_COEFF  = 0.25f;
static constexpr float DIFFUSE_COEFF  = 0.85f;
static constexpr float SPECULAR_COEFF = 0.12f;
static constexpr float DEFAULT_SHININESS = 64.0f;

// Texture alpha discard threshold (shader-side)
static constexpr float ALPHA_DISCARD_THRESHOLD = 0.3f;

// Cache GC: entries unused for this many frames are destroyed
static constexpr int CACHE_UNUSED_FRAME_THRESHOLD = 3;

// Safety limit: skip commands with absurd vertex counts
static constexpr int MAX_VERTEX_COUNT = 10000000;

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

static SbBool
coin_render_ir_trace_enabled()
{
  static int initialized = 0;
  static SbBool enabled = FALSE;
  if (!initialized) {
    enabled = coin_getenv("COIN_DEBUG_RENDER_IR") ? TRUE : FALSE;
    initialized = 1;
  }
  return enabled;
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
coin_compile_shader(const cc_glglue * glue, GLenum type, const char * source)
{
  GLuint shader = cc_glglue_glCreateShader(glue, type);
  cc_glglue_glShaderSource(glue, shader, 1, &source, nullptr);
  cc_glglue_glCompileShader(glue, shader);
  GLint status = GL_FALSE;
  cc_glglue_glGetShaderiv(glue, shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    GLint length = 0;
    cc_glglue_glGetShaderiv(glue, shader, GL_INFO_LOG_LENGTH, &length);
    if (length > 0) {
      std::string log(length, '\0');
      cc_glglue_glGetShaderInfoLog(glue, shader, length, &length, &log[0]);
      SoDebugError::postInfo("SoGLRenderBackend::compileShader", "%s", log.c_str());
    }
    cc_glglue_glDeleteShader(glue, shader);
    return 0;
  }
  return shader;
}

static GLuint
coin_link_program(const cc_glglue * glue, GLuint vs, GLuint fs)
{
  GLuint program = cc_glglue_glCreateProgram(glue);
  cc_glglue_glAttachShader(glue, program, vs);
  cc_glglue_glAttachShader(glue, program, fs);
  cc_glglue_glLinkProgram(glue, program);
  GLint status = GL_FALSE;
  cc_glglue_glGetGLSLProgramiv(glue, program, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    GLint length = 0;
    cc_glglue_glGetGLSLProgramiv(glue, program, GL_INFO_LOG_LENGTH, &length);
    if (length > 0) {
      std::string log(length, '\0');
      cc_glglue_glGetProgramInfoLog(glue, program, length, &length, &log[0]);
      SoDebugError::postInfo("SoGLRenderBackend::linkProgram", "%s", log.c_str());
    }
    cc_glglue_glDeleteProgram(glue, program);
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

inline GLenum blendFactorToGL(SoBlendFactor factor)
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
  case SO_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR: return GL_ONE_MINUS_CONSTANT_COLOR;
  case SO_BLEND_FACTOR_CONSTANT_ALPHA: return GL_CONSTANT_ALPHA;
  case SO_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA: return GL_ONE_MINUS_CONSTANT_ALPHA;
  case SO_BLEND_FACTOR_SRC_ALPHA_SATURATE: return GL_SRC_ALPHA_SATURATE;
  default: return GL_ONE;
  }
}

inline GLenum blendEquationToGL(SoBlendEquation equation)
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

inline GLenum textureWrapToGL(SoTextureWrap wrap)
{
  switch (wrap) {
  case SO_TEXTURE_WRAP_REPEAT: return GL_REPEAT;
  case SO_TEXTURE_WRAP_CLAMP_TO_BORDER: return GL_CLAMP_TO_BORDER;
  case SO_TEXTURE_WRAP_CLAMP_TO_EDGE:
  default: return GL_CLAMP_TO_EDGE;
  }
}

void
applyBlendState(const cc_glglue * glue,
                const SoBlendState & state,
                bool enabled,
                bool useStateFactors)
{
  if (!enabled) {
    glDisable(GL_BLEND);
    return;
  }

  glEnable(GL_BLEND);
  const GLenum srcRGB = useStateFactors
    ? blendFactorToGL(state.srcRGBFactor) : GL_SRC_ALPHA;
  const GLenum dstRGB = useStateFactors
    ? blendFactorToGL(state.dstRGBFactor) : GL_ONE_MINUS_SRC_ALPHA;
  const GLenum srcAlpha = useStateFactors
    ? blendFactorToGL(state.srcAlphaFactor) : GL_SRC_ALPHA;
  const GLenum dstAlpha = useStateFactors
    ? blendFactorToGL(state.dstAlphaFactor) : GL_ONE_MINUS_SRC_ALPHA;
  if (cc_glglue_has_blendfuncseparate(glue)) {
    cc_glglue_glBlendFuncSeparate(glue, srcRGB, dstRGB, srcAlpha, dstAlpha);
  }
  else {
    glBlendFunc(srcRGB, dstRGB);
  }

  // The current renderer-neutral traversal captures ADD for both channels,
  // but keep the backend mapping ready for future separate equations.
  if (cc_glglue_has_blendequation(glue)) {
    cc_glglue_glBlendEquation(glue, blendEquationToGL(state.rgbEquation));
  }
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

  void * currentContext = coin_gl_current_context();
  this->glue = currentContext ?
    cc_glglue_instance_from_context_ptr(currentContext) : nullptr;
  if (!this->glue || !this->glue->glGenVertexArrays ||
      !this->glue->glBindVertexArray || !this->glue->glDeleteVertexArrays ||
      !this->glue->glBindAttribLocation ||
      !this->glue->glGetAttribLocation ||
      !this->glue->glVertexAttrib1f || !this->glue->glVertexAttrib2f ||
      !this->glue->glVertexAttrib3f || !this->glue->glVertexAttrib4f ||
      !this->glue->glVertexAttribPointer ||
      !this->glue->glEnableVertexAttribArray ||
      !this->glue->glDisableVertexAttribArray ||
      !this->glue->glUniform1f || !this->glue->glUniform2f ||
      !this->glue->glUniform3f || !this->glue->glUniform4f ||
      !this->glue->glUniform1i || !this->glue->glUniform1iv ||
      !this->glue->glUniform2fv || !this->glue->glUniform3fv ||
      !this->glue->glUniformMatrix4fv) {
    this->emitError("active context does not provide retained-renderer GL dispatch");
    this->glue = nullptr;
    return FALSE;
  }
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer),
                "target=%dx%d samples=%d id=%u",
                params.targetInfo.size[0], params.targetInfo.size[1],
                params.targetInfo.samples, params.targetInfo.targetId);
  this->emitLog(buffer);

  if (!this->createShaders()) {
    this->emitError("failed to create ModernGL shader");
    return FALSE;
  }

  // Cache attribute locations
  this->posLoc = this->glue->glGetAttribLocation(this->shaderProgram, "a_position");
  this->normLoc = this->glue->glGetAttribLocation(this->shaderProgram, "a_normal");
  this->colorLoc = this->glue->glGetAttribLocation(this->shaderProgram, "a_color");

  this->setInitialized(TRUE);
  return TRUE;
}

void
SoGLRenderBackend::shutdown()
{
  if (!this->isInitialized()) {
    return;
  }
  // Destroy all cached GPU resources
  for (auto & entry : gpuCache) {
    destroyCacheEntry(entry);
  }
  gpuCache.clear();
  ptrToCacheIndex.clear();

  if (this->shaderProgram) {
    cc_glglue_glDeleteProgram(this->glue, this->shaderProgram);
    this->shaderProgram = 0;
  }
  if (this->lineShaderProgram) {
    cc_glglue_glDeleteProgram(this->glue, this->lineShaderProgram);
    this->lineShaderProgram = 0;
  }
  if (this->pointShaderProgram) {
    cc_glglue_glDeleteProgram(this->glue, this->pointShaderProgram);
    this->pointShaderProgram = 0;
  }
  this->setInitialized(FALSE);
  this->emitLog("shutdown");
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
  GLsizei stride = static_cast<GLsizei>(
    cmd.geometry.vertexStride ? cmd.geometry.vertexStride : sizeof(float) * 3);

  // Position VBO
  if (entry.posVBO == 0) cc_glglue_glGenBuffers(this->glue, 1, &entry.posVBO);
  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.posVBO);
  cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(cmd.geometry.vertexCount) * stride,
               cmd.geometry.positions, GL_STATIC_DRAW);

  // Normal VBO — may be smaller than position VBO for BRep shapes
  // (coordinate node includes edge/point vertices that lack normals)
  if (cmd.geometry.normals && cmd.geometry.normalCount > 0) {
    if (entry.normVBO == 0) cc_glglue_glGenBuffers(this->glue, 1, &entry.normVBO);
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.normVBO);
    cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cmd.geometry.normalCount) * stride,
                 cmd.geometry.normals, GL_STATIC_DRAW);
  }

  // Per-vertex color VBO (RGBA float, 4 components per vertex)
  if (cmd.geometry.colors && cmd.geometry.vertexCount > 0) {
    if (entry.colorVBO == 0) cc_glglue_glGenBuffers(this->glue, 1, &entry.colorVBO);
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.colorVBO);
    cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER,
                 cmd.geometry.vertexCount * sizeof(float) * 4,
                 cmd.geometry.colors, GL_STATIC_DRAW);
  }
  else if (entry.colorVBO != 0) {
    cc_glglue_glDeleteBuffers(this->glue, 1, &entry.colorVBO);
    entry.colorVBO = 0;
  }

  // Texcoord VBO + texture upload (for SoImage etc.)
  if (cmd.geometry.texcoords && cmd.material.texture.pixels
      && cmd.geometry.vertexCount > 0) {
    // Texcoord VBO — extract vec2 from vec4 texcoords
    if (entry.texcoordVBO == 0) cc_glglue_glGenBuffers(this->glue, 1, &entry.texcoordVBO);
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.texcoordVBO);
    uint32_t tcStride = cmd.geometry.texcoordStride
      ? cmd.geometry.texcoordStride : sizeof(float) * 4;
    if (tcStride == sizeof(float) * 4) {
      std::vector<float> tc2(cmd.geometry.vertexCount * 2);
      const float * src = cmd.geometry.texcoords;
      for (uint32_t i = 0; i < cmd.geometry.vertexCount; i++) {
        tc2[i * 2] = src[i * 4];
        tc2[i * 2 + 1] = src[i * 4 + 1];
      }
      cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER, tc2.size() * sizeof(float),
                   tc2.data(), GL_STATIC_DRAW);
    } else {
      cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER,
                   cmd.geometry.vertexCount * sizeof(float) * 2,
                   cmd.geometry.texcoords, GL_STATIC_DRAW);
    }

    // Upload texture — expand 1/2-component to RGBA on CPU to avoid
    // GL_LUMINANCE/GL_LUMINANCE_ALPHA which are removed in Core Profile.
    if (entry.textureId == 0) cc_glglue_glGenTextures(this->glue, 1, &entry.textureId);
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, entry.textureId);
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
    const GLenum minFilter = cmd.material.texture.minFilter == SO_TEXTURE_FILTER_LINEAR
      || cmd.material.texture.minFilter == SO_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST
      || cmd.material.texture.minFilter == SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR
      ? GL_LINEAR : GL_NEAREST;
    const GLenum magFilter = cmd.material.texture.magFilter == SO_TEXTURE_FILTER_LINEAR
      || cmd.material.texture.magFilter == SO_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST
      || cmd.material.texture.magFilter == SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR
      ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, 0);
  }
  else {
    // No texture on this command — clean up stale texture state from
    // a previous command that used the same cache entry (pool address reuse).
    if (entry.textureId) {
      cc_glglue_glDeleteTextures(this->glue, 1, &entry.textureId);
      entry.textureId = 0;
    }
    if (entry.texcoordVBO) {
      cc_glglue_glDeleteBuffers(this->glue, 1, &entry.texcoordVBO);
      entry.texcoordVBO = 0;
    }
  }

  // Index VBO
  if (cmd.geometry.indexCount > 0 && cmd.geometry.indices) {
    if (entry.idxVBO == 0) cc_glglue_glGenBuffers(this->glue, 1, &entry.idxVBO);
    cc_glglue_glBindBuffer(this->glue, GL_ELEMENT_ARRAY_BUFFER, entry.idxVBO);
    cc_glglue_glBufferData(this->glue, GL_ELEMENT_ARRAY_BUFFER,
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

    if (entry.lineDistVBO == 0) cc_glglue_glGenBuffers(this->glue, 1, &entry.lineDistVBO);
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.lineDistVBO);
    cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER, vCount * sizeof(float),
                 dist.data(), GL_STATIC_DRAW);
  }
  else if (entry.lineDistVBO) {
    cc_glglue_glDeleteBuffers(this->glue, 1, &entry.lineDistVBO);
    entry.lineDistVBO = 0;
  }

  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, 0);
  cc_glglue_glBindBuffer(this->glue, GL_ELEMENT_ARRAY_BUFFER, 0);

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
  if (entry.vao == 0) this->glue->glGenVertexArrays(1, &entry.vao);
  this->glue->glBindVertexArray(entry.vao);

  GLsizei stride = static_cast<GLsizei>(entry.vertexStride);

  // Position attribute
  if (this->posLoc >= 0 && entry.posVBO) {
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.posVBO);
    cc_glglue_glEnableVertexAttribArray(this->glue, this->posLoc);
    cc_glglue_glVertexAttribPointer(this->glue, this->posLoc, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
  }

  // Normal attribute
  if (this->normLoc >= 0) {
    if (entry.normVBO) {
      cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.normVBO);
      cc_glglue_glEnableVertexAttribArray(this->glue, this->normLoc);
      cc_glglue_glVertexAttribPointer(this->glue, this->normLoc, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    }
    else {
      cc_glglue_glDisableVertexAttribArray(this->glue, this->normLoc);
      cc_glglue_glVertexAttrib3f(this->glue, this->normLoc, 0.0f, 0.0f, 1.0f);
    }
  }

  // Per-vertex color attribute
  if (this->colorLoc >= 0) {
    if (entry.colorVBO) {
      cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.colorVBO);
      cc_glglue_glEnableVertexAttribArray(this->glue, this->colorLoc);
      cc_glglue_glVertexAttribPointer(this->glue, this->colorLoc, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    else {
      cc_glglue_glDisableVertexAttribArray(this->glue, this->colorLoc);
      cc_glglue_glVertexAttrib4f(this->glue, this->colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    }
  }

  // Line distance attribute (cumulative object-space distance for stipple)
  if (this->lineDistLoc >= 0) {
    if (entry.lineDistVBO) {
      cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.lineDistVBO);
      cc_glglue_glEnableVertexAttribArray(this->glue, this->lineDistLoc);
      cc_glglue_glVertexAttribPointer(this->glue, this->lineDistLoc, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    else {
      cc_glglue_glDisableVertexAttribArray(this->glue, this->lineDistLoc);
      cc_glglue_glVertexAttrib1f(this->glue, this->lineDistLoc, 0.0f);
    }
  }

  // Texcoord attribute (for textured commands — billboard/world-space)
  if (this->texcoordLoc >= 0) {
    if (entry.texcoordVBO) {
      cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.texcoordVBO);
      cc_glglue_glEnableVertexAttribArray(this->glue, this->texcoordLoc);
      cc_glglue_glVertexAttribPointer(this->glue, this->texcoordLoc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    else {
      cc_glglue_glDisableVertexAttribArray(this->glue, this->texcoordLoc);
      cc_glglue_glVertexAttrib2f(this->glue, this->texcoordLoc, 0.0f, 0.0f);
    }
  }

  // Index buffer
  if (entry.idxVBO) {
    cc_glglue_glBindBuffer(this->glue, GL_ELEMENT_ARRAY_BUFFER, entry.idxVBO);
  }

  this->glue->glBindVertexArray(0);
  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, 0);
  cc_glglue_glBindBuffer(this->glue, GL_ELEMENT_ARRAY_BUFFER, 0);
}

void
SoGLRenderBackend::destroyCacheEntry(CachedGPUCommand & entry)
{
  if (entry.posVBO) { cc_glglue_glDeleteBuffers(this->glue, 1, &entry.posVBO); entry.posVBO = 0; }
  if (entry.normVBO) { cc_glglue_glDeleteBuffers(this->glue, 1, &entry.normVBO); entry.normVBO = 0; }
  if (entry.colorVBO) { cc_glglue_glDeleteBuffers(this->glue, 1, &entry.colorVBO); entry.colorVBO = 0; }
  if (entry.texcoordVBO) { cc_glglue_glDeleteBuffers(this->glue, 1, &entry.texcoordVBO); entry.texcoordVBO = 0; }
  if (entry.lineDistVBO) { cc_glglue_glDeleteBuffers(this->glue, 1, &entry.lineDistVBO); entry.lineDistVBO = 0; }
  if (entry.textureId) { cc_glglue_glDeleteTextures(this->glue, 1, &entry.textureId); entry.textureId = 0; }
  if (entry.idxVBO) { cc_glglue_glDeleteBuffers(this->glue, 1, &entry.idxVBO); entry.idxVBO = 0; }
  if (entry.vao) { this->glue->glDeleteVertexArrays(1, &entry.vao); entry.vao = 0; }
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

  // Per-command model matrix; view/proj from params (auto-clipped) for
  // main scene, or per-command for overlay/background (different camera).
  SbMat modelMat;
  cmd.modelMatrix.getValue(modelMat);
  this->glue->glUniformMatrix4fv(this->uModelLocation, 1, GL_FALSE, &modelMat[0][0]);
  if (cmd.pass == SO_RENDERPASS_OVERLAY) {
    SbMat cmdViewMat, cmdProjMat;
    cmd.viewMatrix.getValue(cmdViewMat);
    cmd.projMatrix.getValue(cmdProjMat);
    this->glue->glUniformMatrix4fv(this->uViewLocation, 1, GL_FALSE, &cmdViewMat[0][0]);
    this->glue->glUniformMatrix4fv(this->uProjLocation, 1, GL_FALSE, &cmdProjMat[0][0]);
  } else {
    this->glue->glUniformMatrix4fv(this->uViewLocation, 1, GL_FALSE, &viewMat[0][0]);
    this->glue->glUniformMatrix4fv(this->uProjLocation, 1, GL_FALSE, &projMat[0][0]);
  }

  // Per-command color — use vertex colors if available
  bool hasVertexColors = (entry.colorVBO != 0);
  const bool isTextured = (entry.textureId != 0 && entry.texcoordVBO != 0);
  const bool isBillboard = isTextured &&
    (cmd.material.flags & SO_MAT_IS_BILLBOARD) != 0;
  this->glue->glUniform1f(this->uUseVertexColorLocation, hasVertexColors ? 1.0f : 0.0f);
  const SbVec4f & diffuse = cmd.material.diffuse;
  this->glue->glUniform4f(this->uColorLocation,
              diffuse[0], diffuse[1], diffuse[2], diffuse[3]);
  this->glue->glUniform1f(this->uVertexColorAlphaIncludesOpacityLocation,
                          cmd.material.vertexColorAlphaIncludesOpacity ? 1.0f : 0.0f);
  this->glue->glUniform1f(this->uTextureAlphaIncludesOpacityLocation,
                          cmd.material.textureAlphaIncludesOpacity ? 1.0f : 0.0f);
  this->glue->glUniform1i(this->uShadingModelLocation,
                          static_cast<GLint>(cmd.material.shadingModel));
  this->glue->glUniform1i(this->uAlphaTestFunctionLocation,
                          static_cast<GLint>(cmd.state.alphaTest.function));
  this->glue->glUniform1f(this->uAlphaTestReferenceLocation,
                          cmd.state.alphaTest.reference);
  this->applyLighting(drawlist, cmd);

  GLenum prim = topologyToGL(cmd.geometry.topology);

  // The background and 2D foreground passes deliberately own depth state at
  // pass level. All ordinary commands execute their retained depth contract.
  const bool passOwnsDepth = cmd.stage == SoRenderStage::Background ||
    (cmd.stage == SoRenderStage::Foreground &&
     cmd.pass == SO_RENDERPASS_OVERLAY && cmd.viewMatrix == params.viewMatrix);
  if (!passOwnsDepth) {
    if (cmd.state.depth.enabled) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthFunc(depthFunctionToGL(cmd.state.depth.func));
    const bool passDisablesDepthWrite =
      cmd.pass == SO_RENDERPASS_TRANSPARENT;
    glDepthMask((cmd.state.depth.writeEnabled && !passDisablesDepthWrite)
                ? GL_TRUE : GL_FALSE);
    float depthNear = std::max(0.0f, std::min(1.0f, cmd.state.depth.range[0]));
    float depthFar = std::max(0.0f, std::min(1.0f, cmd.state.depth.range[1]));
    glDepthRange(depthNear, depthFar);
  }

  const bool passOwnsBlend = cmd.pass == SO_RENDERPASS_TRANSPARENT ||
    (cmd.stage == SoRenderStage::Foreground &&
     cmd.pass == SO_RENDERPASS_OVERLAY);
  applyBlendState(this->glue, cmd.state.blend,
                  cmd.state.blend.enabled || passOwnsBlend || isTextured,
                  cmd.state.blend.enabled);

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
                    || (cmd.material.featureFlags & SO_FEAT_BASE_COLOR));
  this->glue->glUniform1f(this->uRenderModeLocation, flatColor ? 1.0f : 0.0f);


  // Per-command emissive color (added to lighting result)
  const SbVec4f & ec = cmd.material.emissive;
  this->glue->glUniform3f(this->uEmissiveColorLocation, ec[0], ec[1], ec[2]);
  const SbVec4f & ambient = cmd.material.ambient;
  this->glue->glUniform3f(this->uMaterialAmbientLocation,
                          ambient[0], ambient[1], ambient[2]);
  const SbVec4f & specular = cmd.material.specular;
  this->glue->glUniform3f(this->uMaterialSpecularLocation,
                          specular[0], specular[1], specular[2]);
  this->glue->glUniform1f(this->uMaterialShininessLocation,
                          cmd.material.shininess);
  this->glue->glUniform1f(this->uTwoSidedLightingLocation,
                          cmd.material.twoSidedLighting ? 1.0f : 0.0f);

  // Wireframe draw style: render triangles as lines
  uint8_t fillMode = cmd.state.raster.fillMode;
  if (fillMode == 1 && (prim == GL_TRIANGLES || prim == GL_TRIANGLE_STRIP)) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  }
  else if (fillMode == 2 && (prim == GL_TRIANGLES || prim == GL_TRIANGLE_STRIP)) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
  }

  const float dpr = params.devicePixelRatio > 0.0f
    ? params.devicePixelRatio : 1.0f;
  bool usePointShader = false;
  if (prim == GL_POINTS || fillMode == 2) {
    float ps = cmd.state.raster.pointSize;
    if (ps < 1.0f) ps = cmd.state.raster.lineWidth;
    float pointSize = std::max(ps, 1.0f) * dpr;
    if (prim == GL_POINTS && this->pointShaderProgram) {
      usePointShader = true;
      this->bindPointShader(cmd,
                            viewMat,
                            projMat,
                            diffuse,
                            entry.colorVBO != 0,
                            cmd.state.raster.pointShape == SO_POINT_SHAPE_ROUND,
                            pointSize,
                            coin_command_viewport_size(cmd, params));
      this->glue->glUniform1i(this->pointUAlphaTestFunctionLocation,
                              static_cast<GLint>(cmd.state.alphaTest.function));
      this->glue->glUniform1f(this->pointUAlphaTestReferenceLocation,
                              cmd.state.alphaTest.reference);
    } else {
      glPointSize(pointSize);
      this->glue->glUniform1f(this->uRenderModeLocation, 1.0f);
    }
  }
  bool useLineShader = false;
  const bool linePrimitive = prim == GL_LINES || prim == GL_LINE_STRIP;
  if (linePrimitive || fillMode == 1) {
    float lw = std::max(cmd.state.raster.lineWidth, 1.0f) * dpr;
    if (linePrimitive && this->shouldUseWideLineShader(lw)) {
      // The geometry shader accepts GL_LINES input only. Polygon wireframe
      // remains on the ordinary triangle path.
      useLineShader = true;
      cc_glglue_glUseProgram(this->glue, this->lineShaderProgram);
      this->glue->glUniform1i(this->lineUAlphaTestFunctionLocation,
                              static_cast<GLint>(cmd.state.alphaTest.function));
      this->glue->glUniform1f(this->lineUAlphaTestReferenceLocation,
                              cmd.state.alphaTest.reference);
      SbMat modelMat2;
      cmd.modelMatrix.getValue(modelMat2);
      this->glue->glUniformMatrix4fv(this->lineUModelLocation, 1, GL_FALSE, &modelMat2[0][0]);
      if (cmd.pass == SO_RENDERPASS_OVERLAY) {
        SbMat cmdV, cmdP;
        cmd.viewMatrix.getValue(cmdV);
        cmd.projMatrix.getValue(cmdP);
        this->glue->glUniformMatrix4fv(this->lineUViewLocation, 1, GL_FALSE, &cmdV[0][0]);
        this->glue->glUniformMatrix4fv(this->lineUProjLocation, 1, GL_FALSE, &cmdP[0][0]);
      } else {
        this->glue->glUniformMatrix4fv(this->lineUViewLocation, 1, GL_FALSE, &viewMat[0][0]);
        this->glue->glUniformMatrix4fv(this->lineUProjLocation, 1, GL_FALSE, &projMat[0][0]);
      }
      bool hasVC = (entry.colorVBO != 0);
      this->glue->glUniform1f(this->lineUUseVertexColorLocation, hasVC ? 1.0f : 0.0f);
      this->glue->glUniform4f(this->lineUColorLocation,
                  diffuse[0], diffuse[1], diffuse[2], diffuse[3]);
      SbVec2s vpSz = coin_command_viewport_size(cmd, params);
      this->glue->glUniform2f(this->lineUVpSizeLocation,
                  static_cast<float>(vpSz[0]),
                  static_cast<float>(vpSz[1]));
      this->glue->glUniform1f(this->lineULineWidthLocation, lw);
      this->glue->glUniform1f(this->lineUStipplePeriodLocation, 0.0f);
      this->glue->glUniform3f(this->lineUEmissiveColorLocation, 0.0f, 0.0f, 0.0f);
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
      this->glue->glUniform1f(this->lineUStipplePeriodLocation, objectPeriod);
    } else {
      this->glue->glUniform1f(this->uStipplePeriodLocation, objectPeriod);
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
  if (isTextured) {
    // renderMode: 2=billboard, 3=world-space textured
    this->glue->glUniform1f(this->uRenderModeLocation, isBillboard ? 2.0f : 3.0f);

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
      this->glue->glUniform3f(this->uQuadCenterLocation, cx / n, cy / n, cz / n);

      // Texture pixel size and viewport size
      this->glue->glUniform2f(this->uTexSizeLocation,
                  static_cast<float>(cmd.material.texture.width),
                  static_cast<float>(cmd.material.texture.height));
      SbVec2s vpSz = coin_command_viewport_size(cmd, params);
      this->glue->glUniform2f(this->uVpSizeLocation,
                  static_cast<float>(vpSz[0]),
                  static_cast<float>(vpSz[1]));
    }

    cc_glglue_glActiveTexture(this->glue, GL_TEXTURE0);
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, entry.textureId);
    this->glue->glUniform1i(this->uTextureLocation, 0);
    // Modulate texture with diffuse color (MODULATE mode for NaviCube labels).
    // Billboard textures (SoImage, SoText2) use white modulation (pass-through).
    const SbVec4f & diff = cmd.material.diffuse;
    if (isBillboard || hasVertexColors) {
      this->glue->glUniform4f(this->uTexModColorLocation, 1.0f, 1.0f, 1.0f, 1.0f);
    } else {
      this->glue->glUniform4f(this->uTexModColorLocation, diff[0], diff[1], diff[2], diff[3]);
    }
  }

  // Screen-space billboards historically render on top of the scene. Keep
  // that semantic while restoring the retained depth function below.
  if (isBillboard && !passOwnsDepth) {
    glDepthFunc(GL_ALWAYS);
  }

  this->glue->glBindVertexArray(entry.vao);

  // --- Draw call ---
  if (cmd.geometry.indexCount > 0) {
    cc_glglue_glDrawElements(this->glue, prim, cmd.geometry.indexCount, GL_UNSIGNED_INT, nullptr);
  }
  else {
    cc_glglue_glDrawArrays(this->glue, prim, 0, cmd.geometry.vertexCount);
  }

  // --- State restore ---
  if (isTextured) {
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, 0);
    this->glue->glUniform1f(this->uRenderModeLocation, 0.0f);  // restore to lit mode
  }
  if (isBillboard && !passOwnsDepth) {
    glDepthFunc(depthFunctionToGL(cmd.state.depth.func));
  }

  if (useOffset) {
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
  if (useStipple) {
    if (useLineShader) {
      this->glue->glUniform1f(this->lineUStipplePeriodLocation, 0.0f);
    } else {
      this->glue->glUniform1f(this->uStipplePeriodLocation, 0.0f);
    }
  }
  if (fillMode != 0 && (prim == GL_TRIANGLES || prim == GL_TRIANGLE_STRIP)) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }
  if (prim == GL_POINTS || fillMode == 2) {
    if (usePointShader) {
      cc_glglue_glUseProgram(this->glue, this->shaderProgram);
    }
    glPointSize(1.0f);
  }
  if (prim == GL_LINES || prim == GL_LINE_STRIP || fillMode == 1) {
    if (useLineShader) {
      cc_glglue_glUseProgram(this->glue, this->shaderProgram);
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
  cc_glglue_glUseProgram(this->glue, this->pointShaderProgram);

  SbMat modelMat;
  cmd.modelMatrix.getValue(modelMat);
  this->glue->glUniformMatrix4fv(this->pointUModelLocation, 1, GL_FALSE, &modelMat[0][0]);
  if (cmd.pass == SO_RENDERPASS_OVERLAY) {
    SbMat cmdViewMat, cmdProjMat;
    cmd.viewMatrix.getValue(cmdViewMat);
    cmd.projMatrix.getValue(cmdProjMat);
    this->glue->glUniformMatrix4fv(this->pointUViewLocation, 1, GL_FALSE, &cmdViewMat[0][0]);
    this->glue->glUniformMatrix4fv(this->pointUProjLocation, 1, GL_FALSE, &cmdProjMat[0][0]);
  } else {
    this->glue->glUniformMatrix4fv(this->pointUViewLocation, 1, GL_FALSE, &viewMat[0][0]);
    this->glue->glUniformMatrix4fv(this->pointUProjLocation, 1, GL_FALSE, &projMat[0][0]);
  }
  this->glue->glUniform4f(this->pointUColorLocation, color[0], color[1], color[2], color[3]);
  this->glue->glUniform1f(this->pointUUseVertexColorLocation, useVertexColor ? 1.0f : 0.0f);
  this->glue->glUniform1f(this->pointURoundPointsLocation, roundPoints ? 1.0f : 0.0f);
  this->glue->glUniform1f(this->pointUPointSizeLocation, std::max(pointSize, 1.0f));
  this->glue->glUniform2f(this->pointUVpSizeLocation,
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
  glDepthRange(0.0, 1.0);

  cc_glglue_glUseProgram(this->glue, this->shaderProgram);
  coin_apply_default_viewport(params);

  // Upload view and projection matrices (once per frame)
  SbMat viewMat, projMat;
  params.viewMatrix.getValue(viewMat);
  params.projMatrix.getValue(projMat);
  this->glue->glUniformMatrix4fv(this->uViewLocation, 1, GL_FALSE, &viewMat[0][0]);
  this->glue->glUniformMatrix4fv(this->uProjLocation, 1, GL_FALSE, &projMat[0][0]);

  // Default: lighting enabled, no stipple, dielectric PBR
  this->glue->glUniform1f(this->uRenderModeLocation, 0.0f);
  this->glue->glUniform1f(this->uStipplePeriodLocation, 0.0f);
  this->uploadLighting(coin_fallback_lighting());

  // Viewport size for line stipple derivatives and billboard sizing
  SbVec2s vpSz = params.viewport.getViewportSizePixels();
  this->glue->glUniform2f(this->uVpSizeLocation,
              static_cast<float>(vpSz[0]),
              static_cast<float>(vpSz[1]));
}

void
SoGLRenderBackend::uploadLighting(const SoLightingData & lighting)
{
  const SbVec3f & ambient = lighting.ambient;
  this->glue->glUniform3f(this->uAmbientLightLocation, ambient[0], ambient[1], ambient[2]);

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

  this->glue->glUniform1i(this->uLightCountLocation, lightCount);
  this->glue->glUniform1iv(this->uLightTypeLocation, MAX_SHADER_LIGHTS, lightTypes);
  this->glue->glUniform3fv(this->uLightColorLocation, MAX_SHADER_LIGHTS, lightColors);
  this->glue->glUniform3fv(this->uLightDirectionLocation, MAX_SHADER_LIGHTS, lightDirections);
  this->glue->glUniform3fv(this->uLightPositionLocation, MAX_SHADER_LIGHTS, lightPositions);
  this->glue->glUniform3fv(this->uLightAttenuationLocation, MAX_SHADER_LIGHTS, lightAttenuations);
  this->glue->glUniform2fv(this->uLightSpotParamsLocation, MAX_SHADER_LIGHTS, lightSpotParams);
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
    entry.lastUsedFrame = this->currentFrame;
  }
}

void
SoGLRenderBackend::renderOpaquePass(const SoDrawList & drawlist,
                                    const SbMat & viewMat,
                                    const SbMat & projMat,
                                    const SoRenderParams & params)
{
  const int count = drawlist.getNumCommands();
  const auto & order = drawlist.getSortedOrder();

  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);

  for (int si = 0; si < count; ++si) {
    int ci = (si < static_cast<int>(order.size())) ? order[si] : si;
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    if (cmd.pass != SO_RENDERPASS_OPAQUE) continue;
    drawCommand(drawlist, cmd, viewMat, projMat, params);
  }
}

void
SoGLRenderBackend::renderTransparentPass(const SoDrawList & drawlist,
                                         const SbMat & viewMat,
                                         const SbMat & projMat,
                                         const SoRenderParams & params)
{
  const int count = drawlist.getNumCommands();
  const auto & order = drawlist.getSortedOrder();

  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  for (int si = 0; si < count; ++si) {
    int ci = (si < static_cast<int>(order.size())) ? order[si] : si;
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
    if (cmd.pass != SO_RENDERPASS_TRANSPARENT) continue;
    drawCommand(drawlist, cmd, viewMat, projMat, params);
  }

  // Restore default state
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void
SoGLRenderBackend::renderOverlayPass(const SoDrawList & drawlist,
                                     const SbMat & viewMat,
                                     const SbMat & projMat,
                                     const SoRenderParams & params)
{
  const int count = drawlist.getNumCommands();
  const auto & order = drawlist.getSortedOrder();

  // Collect overlay commands, partitioned into 3D (own camera, e.g. NaviCube)
  // and 2D (annotations, constraint labels — use main camera).
  SbMatrix mainView = params.viewMatrix;
  std::vector<int> overlay3D, overlay2D;
  for (int si = 0; si < count; ++si) {
    int ci = (si < static_cast<int>(order.size())) ? order[si] : si;
    const SoRenderCommand & cmd = drawlist.getCommand(ci);
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
      drawCommand(drawlist, drawlist.getCommand(ci), viewMat, projMat, params);
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
      drawCommand(drawlist, drawlist.getCommand(ci), viewMat, projMat, params);
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
SoGLRenderBackend::endFrame()
{
  glDepthRange(0.0, 1.0);
  this->glue->glBindVertexArray(0);
  cc_glglue_glUseProgram(this->glue, 0);
  gcStaleEntries(this->currentFrame);
}


// -----------------------------------------------------------------------
// Render — orchestrator
// -----------------------------------------------------------------------

SbBool
SoGLRenderBackend::render(const SoDrawList & drawlist,
                          const SoRenderParams & params)
{
  if (!this->shaderProgram) return TRUE;
  SbMat viewMat, projMat;
  params.viewMatrix.getValue(viewMat);
  params.projMatrix.getValue(projMat);

  beginFrame(drawlist, params);
  updateGeometryCache(drawlist);
  renderOpaquePass(drawlist, viewMat, projMat, params);
  renderTransparentPass(drawlist, viewMat, projMat, params);
  renderOverlayPass(drawlist, viewMat, projMat, params);
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
  SoRenderBackend::resizeTarget(info);
}


void
SoGLRenderBackend::logFrameStats(const SoDrawList & drawlist,
                                 const SoRenderParams & params) const
{
  const int num = drawlist.getNumCommands();
  SoDebugError::postInfo("SoGLRenderBackend::render",
                         "frame=%d cmds=%d",
                         params.frameIndex, num);
}

bool
SoGLRenderBackend::createShaders()
{
  // Generated from data/shaders/backend/*.glsl.
  static const char * vertexSource = BACKENDUNIFIEDVERTEX_shadersource;
  static const char * fragmentSource = BACKENDUNIFIEDFRAGMENT_shadersource;

  GLuint vs = coin_compile_shader(this->glue, GL_VERTEX_SHADER, vertexSource);
  GLuint fs = coin_compile_shader(this->glue, GL_FRAGMENT_SHADER, fragmentSource);
  if (vs == 0 || fs == 0) {
    cc_glglue_glDeleteShader(this->glue, vs);
    cc_glglue_glDeleteShader(this->glue, fs);
    return FALSE;
  }

  // Bind attribute locations explicitly before linking — the macOS GLSL 1.20
  // compiler may reassign locations when a new attribute (a_texcoord) is added,
  // and we need stable locations that match the VAO setup.
  GLuint prog = cc_glglue_glCreateProgram(this->glue);
  cc_glglue_glAttachShader(this->glue, prog, vs);
  cc_glglue_glAttachShader(this->glue, prog, fs);
  this->glue->glBindAttribLocation(prog, 0, "a_position");
  this->glue->glBindAttribLocation(prog, 1, "a_normal");
  this->glue->glBindAttribLocation(prog, 2, "a_color");
  this->glue->glBindAttribLocation(prog, 3, "a_texcoord");
  this->glue->glBindAttribLocation(prog, 4, "a_lineDistance");
  cc_glglue_glLinkProgram(this->glue, prog);
  GLint linkStatus = GL_FALSE;
  cc_glglue_glGetGLSLProgramiv(this->glue, prog, GL_LINK_STATUS, &linkStatus);
  if (linkStatus == GL_FALSE) {
    GLint length = 0;
    cc_glglue_glGetGLSLProgramiv(this->glue, prog, GL_INFO_LOG_LENGTH, &length);
    if (length > 0) {
      std::string log(length, '\0');
      cc_glglue_glGetProgramInfoLog(this->glue, prog, length, &length, &log[0]);
      SoDebugError::postInfo("SoGLRenderBackend::linkProgram", "%s", log.c_str());
    }
    cc_glglue_glDeleteProgram(this->glue, prog);
    cc_glglue_glDeleteShader(this->glue, vs);
    cc_glglue_glDeleteShader(this->glue, fs);
    return FALSE;
  }
  this->shaderProgram = prog;
  cc_glglue_glDeleteShader(this->glue, vs);
  cc_glglue_glDeleteShader(this->glue, fs);

  this->uViewLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_view");
  this->uProjLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_proj");
  this->uModelLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_model");
  this->uColorLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_color");
  this->uRenderModeLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_renderMode");
  this->uEmissiveColorLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_emissiveColor");
  this->uUseVertexColorLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_useVertexColor");
  this->uTextureLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_texture");
  this->uTexModColorLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_texModColor");
  this->uVertexColorAlphaIncludesOpacityLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_vertexColorAlphaIncludesOpacity");
  this->uTextureAlphaIncludesOpacityLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_textureAlphaIncludesOpacity");
  this->uAlphaTestFunctionLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_alphaTestFunction");
  this->uAlphaTestReferenceLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_alphaTestReference");
  this->uQuadCenterLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_quadCenter");
  this->uTexSizeLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_texSize");
  this->uVpSizeLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_vpSize");
  this->uStipplePeriodLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_stipplePeriod");
  this->uShadingModelLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_shadingModel");
  this->uAmbientLightLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_ambientLight");
  this->uMaterialAmbientLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_materialAmbient");
  this->uMaterialSpecularLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_materialSpecular");
  this->uMaterialShininessLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_materialShininess");
  this->uTwoSidedLightingLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_twoSidedLighting");
  this->uLightCountLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_lightCount");
  this->uLightTypeLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_lightType[0]");
  this->uLightColorLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_lightColor[0]");
  this->uLightDirectionLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_lightDirection[0]");
  this->uLightPositionLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_lightPosition[0]");
  this->uLightAttenuationLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_lightAttenuation[0]");
  this->uLightSpotParamsLocation = cc_glglue_glGetUniformLocation(this->glue, this->shaderProgram, "u_lightSpotParams[0]");
  this->texcoordLoc = this->glue->glGetAttribLocation(this->shaderProgram, "a_texcoord");
  this->lineDistLoc = this->glue->glGetAttribLocation(this->shaderProgram, "a_lineDistance");

  // Line shader — geometry shader expands lines into screen-space quads
  // for wide lines on Core Profile where glLineWidth is clamped to 1.0.
  static const char * lineVertSource = BACKENDWIDELINEVERTEX_shadersource;
  static const char * lineGeomSource = BACKENDWIDELINEGEOMETRY_shadersource;
  static const char * lineFragSource = BACKENDWIDELINEFRAGMENT_shadersource;

  GLuint lvs = coin_compile_shader(this->glue, GL_VERTEX_SHADER, lineVertSource);
  GLuint lgs = coin_compile_shader(this->glue, GL_GEOMETRY_SHADER, lineGeomSource);
  GLuint lfs = coin_compile_shader(this->glue, GL_FRAGMENT_SHADER, lineFragSource);
  if (lvs && lgs && lfs) {
    GLuint lprog = cc_glglue_glCreateProgram(this->glue);
    cc_glglue_glAttachShader(this->glue, lprog, lvs);
    cc_glglue_glAttachShader(this->glue, lprog, lgs);
    cc_glglue_glAttachShader(this->glue, lprog, lfs);
    this->glue->glBindAttribLocation(lprog, 0, "a_position");
    this->glue->glBindAttribLocation(lprog, 2, "a_color");
    this->glue->glBindAttribLocation(lprog, 4, "a_lineDistance");
    cc_glglue_glLinkProgram(this->glue, lprog);
    GLint linkOk = GL_FALSE;
    cc_glglue_glGetGLSLProgramiv(this->glue, lprog, GL_LINK_STATUS, &linkOk);
    if (linkOk) {
      this->lineShaderProgram = lprog;
      this->lineUViewLocation = cc_glglue_glGetUniformLocation(this->glue, lprog, "u_view");
      this->lineUProjLocation = cc_glglue_glGetUniformLocation(this->glue, lprog, "u_proj");
      this->lineUModelLocation = cc_glglue_glGetUniformLocation(this->glue, lprog, "u_model");
      this->lineUColorLocation = cc_glglue_glGetUniformLocation(this->glue, lprog, "u_color");
      this->lineULineWidthLocation = cc_glglue_glGetUniformLocation(this->glue, lprog, "u_lineWidth");
      this->lineUVpSizeLocation = cc_glglue_glGetUniformLocation(this->glue, lprog, "u_vpSize");
      this->lineURenderModeLocation = cc_glglue_glGetUniformLocation(this->glue, lprog, "u_renderMode");
      this->lineUStipplePeriodLocation = cc_glglue_glGetUniformLocation(this->glue, lprog, "u_stipplePeriod");
      this->lineUEmissiveColorLocation = cc_glglue_glGetUniformLocation(this->glue, lprog, "u_emissiveColor");
      this->lineUUseVertexColorLocation = cc_glglue_glGetUniformLocation(this->glue, lprog, "u_useVertexColor");
      this->lineUAlphaTestFunctionLocation = cc_glglue_glGetUniformLocation(this->glue, lprog, "u_alphaTestFunction");
      this->lineUAlphaTestReferenceLocation = cc_glglue_glGetUniformLocation(this->glue, lprog, "u_alphaTestReference");
    } else {
      cc_glglue_glDeleteProgram(this->glue, lprog);
    }
  }
  cc_glglue_glDeleteShader(this->glue, lvs);
  cc_glglue_glDeleteShader(this->glue, lgs);
  cc_glglue_glDeleteShader(this->glue, lfs);

  // Point shader — geometry shader expands points into screen-space quads
  // for stable marker sizing and circular vertex overlays.
  static const char * pointVertSource = BACKENDWIDEPOINTVERTEX_shadersource;
  static const char * pointGeomSource = BACKENDWIDEPOINTGEOMETRY_shadersource;
  static const char * pointFragSource = BACKENDWIDEPOINTFRAGMENT_shadersource;

  GLuint pvs = coin_compile_shader(this->glue, GL_VERTEX_SHADER, pointVertSource);
  GLuint pgs = coin_compile_shader(this->glue, GL_GEOMETRY_SHADER, pointGeomSource);
  GLuint pfs = coin_compile_shader(this->glue, GL_FRAGMENT_SHADER, pointFragSource);
  if (pvs && pgs && pfs) {
    GLuint pprog = cc_glglue_glCreateProgram(this->glue);
    cc_glglue_glAttachShader(this->glue, pprog, pvs);
    cc_glglue_glAttachShader(this->glue, pprog, pgs);
    cc_glglue_glAttachShader(this->glue, pprog, pfs);
    this->glue->glBindAttribLocation(pprog, 0, "a_position");
    this->glue->glBindAttribLocation(pprog, 2, "a_color");
    cc_glglue_glLinkProgram(this->glue, pprog);
    GLint linkOk = GL_FALSE;
    cc_glglue_glGetGLSLProgramiv(this->glue, pprog, GL_LINK_STATUS, &linkOk);
    if (linkOk) {
      this->pointShaderProgram = pprog;
      this->pointUViewLocation = cc_glglue_glGetUniformLocation(this->glue, pprog, "u_view");
      this->pointUProjLocation = cc_glglue_glGetUniformLocation(this->glue, pprog, "u_proj");
      this->pointUModelLocation = cc_glglue_glGetUniformLocation(this->glue, pprog, "u_model");
      this->pointUColorLocation = cc_glglue_glGetUniformLocation(this->glue, pprog, "u_color");
      this->pointUPointSizeLocation = cc_glglue_glGetUniformLocation(this->glue, pprog, "u_pointSize");
      this->pointURoundPointsLocation = cc_glglue_glGetUniformLocation(this->glue, pprog, "u_roundPoints");
      this->pointUVpSizeLocation = cc_glglue_glGetUniformLocation(this->glue, pprog, "u_vpSize");
      this->pointUUseVertexColorLocation = cc_glglue_glGetUniformLocation(this->glue, pprog, "u_useVertexColor");
      this->pointUAlphaTestFunctionLocation = cc_glglue_glGetUniformLocation(this->glue, pprog, "u_alphaTestFunction");
      this->pointUAlphaTestReferenceLocation = cc_glglue_glGetUniformLocation(this->glue, pprog, "u_alphaTestReference");
    } else {
      cc_glglue_glDeleteProgram(this->glue, pprog);
    }
  }
  cc_glglue_glDeleteShader(this->glue, pvs);
  cc_glglue_glDeleteShader(this->glue, pgs);
  cc_glglue_glDeleteShader(this->glue, pfs);

  return TRUE;
}
