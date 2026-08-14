// src/rendering/SoGLRenderBackend.cpp

#include "rendering/SoGLRenderBackend.h"

#include <Inventor/C/glue/gl.h>
#include <Inventor/SbName.h>
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/misc/SoGLDriverDatabase.h>

#include "glue/glp.h"
#include "glue/glslp.h"

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>
#include <string>
#include <vector>

#include <data/shaders/gl/visual/Fragment.h>
#include <data/shaders/gl/visual/Vertex.h>
#include <data/shaders/gl/wide-line/Fragment.h>
#include <data/shaders/gl/wide-line/Geometry.h>
#include <data/shaders/gl/wide-line/TriangleGeometry.h>
#include <data/shaders/gl/wide-line/Vertex.h>
#include <data/shaders/gl/point/Fragment.h>
#include <data/shaders/gl/point/Geometry.h>
#include <data/shaders/gl/point/TriangleGeometry.h>
#include <data/shaders/gl/point/Vertex.h>
#include <data/shaders/gl/pixel/Fragment.h>
#include <data/shaders/gl/pixel/Vertex.h>

namespace {

static constexpr int MAX_VERTEX_COUNT = 10000000;
static constexpr int MAX_SHADER_LIGHTS = 8;
static constexpr GLuint POSITION_ATTRIBUTE = 0;
static constexpr GLuint NORMAL_ATTRIBUTE = 1;
static constexpr GLuint COLOR_ATTRIBUTE = 2;
static constexpr GLuint TEXCOORD_ATTRIBUTE = 3;
static constexpr GLuint LINE_DISTANCE_ATTRIBUTE = 4;

GLenum
textureWrapToGL(const SoTextureWrap wrap)
{
  switch (wrap) {
  case SO_TEXTURE_WRAP_REPEAT: return GL_REPEAT;
  case SO_TEXTURE_WRAP_CLAMP_TO_BORDER: return GL_CLAMP_TO_BORDER;
  case SO_TEXTURE_WRAP_CLAMP_TO_EDGE:
  default: return GL_CLAMP_TO_EDGE;
  }
}

GLenum
textureMinFilterToGL(const SoTextureFilter filter)
{
  switch (filter) {
  case SO_TEXTURE_FILTER_LINEAR: return GL_LINEAR;
  case SO_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
    return GL_NEAREST_MIPMAP_NEAREST;
  case SO_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
    return GL_LINEAR_MIPMAP_NEAREST;
  case SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
    return GL_NEAREST_MIPMAP_LINEAR;
  case SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
    return GL_LINEAR_MIPMAP_LINEAR;
  case SO_TEXTURE_FILTER_NEAREST:
  default: return GL_NEAREST;
  }
}

GLenum
textureMagFilterToGL(const SoTextureFilter filter)
{
  return filter == SO_TEXTURE_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR;
}

GLenum
blendFactorToGL(const SoBlendFactor factor)
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
  // The Visual program has no secondary fragment output. Keep the semantic
  // factor in the IR and make the executor's deterministic primary-source
  // fallback only at this API boundary.
  case SO_BLEND_FACTOR_SRC1_COLOR: return GL_SRC_COLOR;
  case SO_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR: return GL_ONE_MINUS_SRC_COLOR;
  case SO_BLEND_FACTOR_SRC1_ALPHA: return GL_SRC_ALPHA;
  case SO_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
  default: return GL_ONE;
  }
}

bool
isDualSourceBlendFactor(const SoBlendFactor factor)
{
  return factor == SO_BLEND_FACTOR_SRC1_COLOR ||
         factor == SO_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR ||
         factor == SO_BLEND_FACTOR_SRC1_ALPHA ||
         factor == SO_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
}

GLenum
blendEquationToGL(const SoBlendEquation equation)
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

GLenum
depthFunctionToGL(const SoDepthFunction function)
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

GLenum
topologyToGL(const SoPrimitiveTopology topology)
{
  switch (topology) {
  case SO_TOPOLOGY_POINTS: return GL_POINTS;
  case SO_TOPOLOGY_LINES: return GL_LINES;
  case SO_TOPOLOGY_TRIANGLES: return GL_TRIANGLES;
  case SO_TOPOLOGY_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
  case SO_TOPOLOGY_LINE_STRIP: return GL_LINE_STRIP;
  default: return GL_TRIANGLES;
  }
}

void
applyViewport(const SoRenderParams & params)
{
  const SbVec2s & origin = params.viewport.getViewportOriginPixels();
  const SbVec2s & size = params.viewport.getViewportSizePixels();
  glViewport(origin[0], origin[1], size[0], size[1]);
}

void
logShaderSourceMap(const char * source)
{
  const std::string marker = "// coin-source-id: ";
  const std::string sourceText = source ? source : "";
  std::string::size_type position = 0;
  while ((position = sourceText.find(marker, position)) != std::string::npos) {
    const std::string::size_type end = sourceText.find('\n', position);
    const std::string mapping = sourceText.substr(
      position + marker.length(),
      end == std::string::npos ? std::string::npos : end - position - marker.length());
    SoDebugError::postInfo("SoGLRenderBackend::compileShader",
                           "source ID map: %s", mapping.c_str());
    position = end == std::string::npos ? sourceText.length() : end + 1;
  }
}

GLuint
compileShader(const cc_glglue * glue, const GLenum type, const char * source)
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
      std::string log(static_cast<size_t>(length), '\0');
      cc_glglue_glGetShaderInfoLog(glue, shader, length, &length, &log[0]);
      SoDebugError::postInfo("SoGLRenderBackend::compileShader",
                             "%s", log.c_str());
    }
    logShaderSourceMap(source);
    cc_glglue_glDeleteShader(glue, shader);
    return 0;
  }
  return shader;
}

GLuint
linkProgram(const cc_glglue * glue, const char * vertexSource,
            const char * fragmentSource, const char * geometrySource = nullptr)
{
  const GLuint vertex = compileShader(glue, GL_VERTEX_SHADER, vertexSource);
  const GLuint fragment = compileShader(glue, GL_FRAGMENT_SHADER, fragmentSource);
  const GLuint geometry = geometrySource
    ? compileShader(glue, GL_GEOMETRY_SHADER, geometrySource) : 0;
  if (!vertex || !fragment || (geometrySource && !geometry)) {
    if (vertex) cc_glglue_glDeleteShader(glue, vertex);
    if (fragment) cc_glglue_glDeleteShader(glue, fragment);
    if (geometry) cc_glglue_glDeleteShader(glue, geometry);
    return 0;
  }

  const GLuint program = cc_glglue_glCreateProgram(glue);
  cc_glglue_glAttachShader(glue, program, vertex);
  cc_glglue_glAttachShader(glue, program, fragment);
  if (geometry) cc_glglue_glAttachShader(glue, program, geometry);
  cc_glglue_glLinkProgram(glue, program);
  GLint linked = GL_FALSE;
  cc_glglue_glGetGLSLProgramiv(glue, program, GL_LINK_STATUS, &linked);
  if (linked == GL_FALSE) {
    GLint length = 0;
    cc_glglue_glGetGLSLProgramiv(glue, program, GL_INFO_LOG_LENGTH, &length);
    if (length > 0) {
      std::string log(static_cast<size_t>(length), '\0');
      cc_glglue_glGetProgramInfoLog(glue, program, length, &length, &log[0]);
      SoDebugError::postInfo("SoGLRenderBackend::linkProgram", "%s",
                             log.c_str());
    }
    cc_glglue_glDeleteProgram(glue, program);
  }
  cc_glglue_glDeleteShader(glue, vertex);
  cc_glglue_glDeleteShader(glue, fragment);
  if (geometry) cc_glglue_glDeleteShader(glue, geometry);
  return linked == GL_FALSE ? 0 : program;
}

struct TextureUploadFormat {
  GLint internalFormat;
  GLenum format;
  GLint swizzle[4];
};

TextureUploadFormat
textureUploadFormat(const int components, const SoTextureColorSpace colorSpace)
{
  const bool srgb = colorSpace == SO_TEXTURE_COLORSPACE_SRGB;
  switch (components) {
  case 1:
    return { GL_R8, GL_RED, { GL_RED, GL_RED, GL_RED, GL_ONE } };
  case 2:
    return { GL_RG8, GL_RG, { GL_RED, GL_RED, GL_RED, GL_GREEN } };
  case 3:
    return { srgb ? GL_SRGB8 : GL_RGB8, GL_RGB,
             { GL_RED, GL_GREEN, GL_BLUE, GL_ONE } };
  case 4:
  default:
    return { srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, GL_RGBA,
             { GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA } };
  }
}

class ScopedPixelUnpackState {
public:
  explicit ScopedPixelUnpackState(const cc_glglue * glue)
    : glue(glue)
  {
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &pixelUnpackBuffer);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    cc_glglue_glBindBuffer(this->glue, GL_PIXEL_UNPACK_BUFFER, 0);
  }

  ~ScopedPixelUnpackState()
  {
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, skipPixels);
    cc_glglue_glBindBuffer(this->glue, GL_PIXEL_UNPACK_BUFFER,
                           static_cast<GLuint>(pixelUnpackBuffer));
  }

private:
  const cc_glglue * glue;
  GLint alignment = 4;
  GLint rowLength = 0;
  GLint skipRows = 0;
  GLint skipPixels = 0;
  GLint pixelUnpackBuffer = 0;
};

} // namespace

SoGLRenderBackend::SoGLRenderBackend()
{
}

SoGLRenderBackend::~SoGLRenderBackend() = default;

const char *
SoGLRenderBackend::getName() const
{
  return "GLRenderBackend";
}

SbBool
SoGLRenderBackend::initialize(const SoRenderBackendInitParams & params)
{
  if (this->isInitialized()) return TRUE;

  this->setInitParams(params);
  void * context = coin_gl_current_context();
  this->glue = context ? cc_glglue_instance_from_context_ptr(context) : nullptr;
  if (!this->glue) {
    this->emitError("active context does not provide retained-renderer GL dispatch");
    return FALSE;
  }
  if (!cc_glglue_glversion_matches_at_least(this->glue, 3, 3, 0)) {
    this->emitError("retained renderer requires OpenGL 3.3 or later");
    this->glue = nullptr;
    return FALSE;
  }
  if (!this->glue->glGenVertexArrays ||
      !this->glue->glBindVertexArray ||
      !this->glue->glDeleteVertexArrays ||
      !this->glue->glGetAttribLocation ||
      !this->glue->glVertexAttribPointer ||
      !this->glue->glEnableVertexAttribArray ||
      !this->glue->glDisableVertexAttribArray ||
      !this->glue->glVertexAttrib4f ||
      !this->glue->glVertexAttrib3f ||
      !this->glue->glVertexAttrib2f ||
      !this->glue->glVertexAttrib1f ||
      !this->glue->glUniform1f || !this->glue->glUniform1i ||
      !this->glue->glUniform2f ||
      !this->glue->glUniform3f || !this->glue->glUniform1iv ||
      !this->glue->glUniform2fv || !this->glue->glUniform3fv ||
      !this->glue->glUniform4f || !this->glue->glUniformMatrix4fv ||
      !this->glue->glBlendFuncSeparate) {
    this->emitError("active context does not provide retained-renderer GL dispatch");
    this->glue = nullptr;
    return FALSE;
  }

  if (!this->createShaders()) {
    this->emitError("failed to create retained OpenGL 3.3 shaders");
    this->glue = nullptr;
    return FALSE;
  }

  GLfloat lineRange[2] = { 1.0f, 1.0f };
  GLfloat pointRange[2] = { 1.0f, 1.0f };
  glGetFloatv(GL_LINE_WIDTH_RANGE, lineRange);
  glGetFloatv(GL_POINT_SIZE_RANGE, pointRange);
  this->rasterPrograms.nativeLineWidthMax = std::max(1.0f, lineRange[1]);
  this->rasterPrograms.nativePointSizeMax = std::max(1.0f, pointRange[1]);
  this->setInitialized(TRUE);
  return TRUE;
}

void
SoGLRenderBackend::destroyCacheEntry(CachedCommand & entry)
{
  this->destroyLineRasterStream(entry);
  if (entry.positionBuffer) {
    cc_glglue_glDeleteBuffers(this->glue, 1, &entry.positionBuffer);
  }
  if (entry.normalBuffer) {
    cc_glglue_glDeleteBuffers(this->glue, 1, &entry.normalBuffer);
  }
  if (entry.colorBuffer) {
    cc_glglue_glDeleteBuffers(this->glue, 1, &entry.colorBuffer);
  }
  if (entry.texcoordBuffer) {
    cc_glglue_glDeleteBuffers(this->glue, 1, &entry.texcoordBuffer);
  }
  if (entry.lineDistanceBuffer) {
    cc_glglue_glDeleteBuffers(this->glue, 1, &entry.lineDistanceBuffer);
  }
  if (entry.texture) {
    cc_glglue_glDeleteTextures(this->glue, 1, &entry.texture);
  }
  if (entry.indexBuffer) {
    cc_glglue_glDeleteBuffers(this->glue, 1, &entry.indexBuffer);
  }
  if (entry.vertexArray) this->glue->glDeleteVertexArrays(1, &entry.vertexArray);
  entry = CachedCommand();
}

void
SoGLRenderBackend::destroyLineRasterStream(CachedCommand & entry)
{
  const GLuint buffers[] = {
    entry.lineRasterPositionBuffer,
    entry.lineRasterNormalBuffer,
    entry.lineRasterColorBuffer,
    entry.lineRasterTexcoordBuffer,
    entry.lineRasterDistanceBuffer
  };
  for (GLuint buffer : buffers) {
    if (buffer) cc_glglue_glDeleteBuffers(this->glue, 1, &buffer);
  }
  if (entry.lineRasterVertexArray) {
    this->glue->glDeleteVertexArrays(1, &entry.lineRasterVertexArray);
  }
  entry.lineRasterVertexArray = 0;
  entry.lineRasterPositionBuffer = 0;
  entry.lineRasterNormalBuffer = 0;
  entry.lineRasterColorBuffer = 0;
  entry.lineRasterTexcoordBuffer = 0;
  entry.lineRasterDistanceBuffer = 0;
  entry.lineRasterVertexCount = 0;
  entry.lineRasterIndexCount = 0;
  entry.lineRasterPositionsKey = nullptr;
  entry.lineRasterNormalsKey = nullptr;
  entry.lineRasterColorsKey = nullptr;
  entry.lineRasterTexcoordsKey = nullptr;
  entry.lineRasterIndicesKey = nullptr;
}

void
SoGLRenderBackend::invalidateCache()
{
  if (this->glue) {
    for (CachedCommand & entry : this->gpuCache) {
      this->destroyCacheEntry(entry);
    }
  }
  this->gpuCache.clear();
  this->commandToCache.clear();
  this->resourceToCache.clear();
  this->cachedCommandCount = 0;
  this->haveCacheGeneration = false;
}

void
SoGLRenderBackend::shutdown()
{
  if (!this->isInitialized()) return;

  this->invalidateCache();
  if (this->visualProgram.handle) {
    cc_glglue_glDeleteProgram(this->glue, this->visualProgram.handle);
    this->visualProgram.handle = 0;
  }
  if (this->rasterPrograms.line.handle) {
    cc_glglue_glDeleteProgram(this->glue, this->rasterPrograms.line.handle);
    this->rasterPrograms.line.handle = 0;
  }
  if (this->rasterPrograms.triangleLine.handle) {
    cc_glglue_glDeleteProgram(this->glue,
                              this->rasterPrograms.triangleLine.handle);
    this->rasterPrograms.triangleLine.handle = 0;
  }
  if (this->rasterPrograms.point.handle) {
    cc_glglue_glDeleteProgram(this->glue, this->rasterPrograms.point.handle);
    this->rasterPrograms.point.handle = 0;
  }
  if (this->rasterPrograms.trianglePoint.handle) {
    cc_glglue_glDeleteProgram(this->glue,
                              this->rasterPrograms.trianglePoint.handle);
    this->rasterPrograms.trianglePoint.handle = 0;
  }
  if (this->rasterPrograms.pixel.handle) {
    cc_glglue_glDeleteProgram(this->glue, this->rasterPrograms.pixel.handle);
    this->rasterPrograms.pixel.handle = 0;
  }
  this->glue = nullptr;
  this->setInitialized(FALSE);
  this->emitLog("shutdown");
}

SoGLRenderBackend::CachedCommand &
SoGLRenderBackend::getOrCreateCache(const SoRenderCommand * command)
{
  const auto found = this->commandToCache.find(command);
  if (found != this->commandToCache.end()) {
    return this->gpuCache[found->second];
  }

  const SoGeometryDesc & geometry = command->geometry;
  const SoTextureData & texture = command->material.texture;
  const bool hasTexture = (texture.cacheKey != 0 || texture.pixels != nullptr) &&
    texture.width > 0 && texture.height > 0 && texture.numComponents >= 1 &&
    texture.numComponents <= 4 && geometry.texcoords && geometry.vertexCount;
  const bool persistent = geometry.cacheKey != 0 &&
    (!hasTexture || texture.cacheKey != 0);
  ResourceCacheKey resourceKey;
  resourceKey.geometry = persistent ? geometry.cacheKey : 0;
  resourceKey.texture = persistent && hasTexture ? texture.cacheKey : 0;
  if (persistent) {
    const auto resource = this->resourceToCache.find(resourceKey);
    if (resource != this->resourceToCache.end()) {
      this->commandToCache[command] = resource->second;
      return this->gpuCache[resource->second];
    }
  }

  const size_t index = this->gpuCache.size();
  this->gpuCache.emplace_back();
  this->commandToCache[command] = index;
  if (persistent) {
    CachedCommand & entry = this->gpuCache.back();
    entry.persistent = true;
    entry.resourceKey = resourceKey;
    this->resourceToCache[resourceKey] = index;
  }
  return this->gpuCache.back();
}

void
SoGLRenderBackend::uploadVertexBuffers(CachedCommand & entry,
                                       const SoGeometryDesc & geometry)
{
  const GLsizei vertexStride = static_cast<GLsizei>(
    geometry.vertexStride ? geometry.vertexStride : sizeof(float) * 3);
  if (!entry.positionBuffer) {
    cc_glglue_glGenBuffers(this->glue, 1, &entry.positionBuffer);
  }
  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.positionBuffer);
  cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(geometry.vertexCount) *
                         vertexStride, geometry.positions, GL_STATIC_DRAW);

  if (geometry.normals && geometry.normalCount >= geometry.vertexCount) {
    if (!entry.normalBuffer) {
      cc_glglue_glGenBuffers(this->glue, 1, &entry.normalBuffer);
    }
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.normalBuffer);
    cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER,
                           static_cast<GLsizeiptr>(geometry.vertexCount) *
                           vertexStride, geometry.normals, GL_STATIC_DRAW);
  }
  else if (entry.normalBuffer) {
    cc_glglue_glDeleteBuffers(this->glue, 1, &entry.normalBuffer);
    entry.normalBuffer = 0;
  }

  if (geometry.colors && geometry.vertexCount) {
    if (!entry.colorBuffer) {
      cc_glglue_glGenBuffers(this->glue, 1, &entry.colorBuffer);
    }
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.colorBuffer);
    cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER,
                           static_cast<GLsizeiptr>(geometry.vertexCount) *
                           sizeof(float) * 4, geometry.colors, GL_STATIC_DRAW);
  }
  else if (entry.colorBuffer) {
    cc_glglue_glDeleteBuffers(this->glue, 1, &entry.colorBuffer);
    entry.colorBuffer = 0;
  }
}

void
SoGLRenderBackend::uploadTexture(CachedCommand & entry,
                                 const SoRenderCommand & command)
{
  const SoGeometryDesc & geometry = command.geometry;
  const SoTextureData & texture = command.material.texture;
  const bool hasTexture = texture.pixels && texture.width > 0 &&
    texture.height > 0 && texture.numComponents >= 1 &&
    texture.numComponents <= 4 && geometry.texcoords && geometry.vertexCount;
  if (!hasTexture) {
    if (entry.texcoordBuffer) {
      cc_glglue_glDeleteBuffers(this->glue, 1, &entry.texcoordBuffer);
      entry.texcoordBuffer = 0;
    }
    if (entry.texture) {
      cc_glglue_glDeleteTextures(this->glue, 1, &entry.texture);
      entry.texture = 0;
    }
    return;
  }

  if (!entry.texcoordBuffer) {
    cc_glglue_glGenBuffers(this->glue, 1, &entry.texcoordBuffer);
  }
  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.texcoordBuffer);
  const uint32_t sourceStride = geometry.texcoordStride
    ? geometry.texcoordStride : sizeof(float) * 4;
  std::vector<float> texcoords(static_cast<size_t>(geometry.vertexCount) * 2);
  const char * raw = reinterpret_cast<const char *>(geometry.texcoords);
  for (uint32_t i = 0; i < geometry.vertexCount; ++i) {
    const float * source = reinterpret_cast<const float *>(
      raw + static_cast<size_t>(i) * sourceStride);
    texcoords[static_cast<size_t>(i) * 2] = source[0];
    texcoords[static_cast<size_t>(i) * 2 + 1] = source[1];
  }
  cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER,
                         texcoords.size() * sizeof(float), texcoords.data(),
                         GL_STATIC_DRAW);

  if (!entry.texture) {
    cc_glglue_glGenTextures(this->glue, 1, &entry.texture);
  }
  cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, entry.texture);
  const TextureUploadFormat format =
    textureUploadFormat(texture.numComponents, texture.colorSpace);
  const ScopedPixelUnpackState unpackState(this->glue);
  glTexImage2D(GL_TEXTURE_2D, 0, format.internalFormat,
               texture.width, texture.height, 0, format.format,
               GL_UNSIGNED_BYTE, texture.pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, format.swizzle[0]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, format.swizzle[1]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, format.swizzle[2]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, format.swizzle[3]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  textureMinFilterToGL(texture.minFilter));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  textureMagFilterToGL(texture.magFilter));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                  textureWrapToGL(texture.wrapS));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                  textureWrapToGL(texture.wrapT));
  const bool mipmapped =
    texture.minFilter == SO_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
    texture.minFilter == SO_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST ||
    texture.minFilter == SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR ||
    texture.minFilter == SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR;
  if (mipmapped && this->glue->glGenerateMipmap) {
    this->glue->glGenerateMipmap(GL_TEXTURE_2D);
  }
  if (SoGLDriverDatabase::isSupported(
        this->glue, SbName(SO_GL_ANISOTROPIC_FILTERING))) {
    const float supported = cc_glglue_get_max_anisotropy(this->glue);
    if (supported > 1.0f) {
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                      texture.anisotropic ? supported : 1.0f);
    }
  }
  cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, 0);
}

void
SoGLRenderBackend::uploadIndices(CachedCommand & entry,
                                 const SoGeometryDesc & geometry)
{
  if (geometry.indexCount && geometry.indices) {
    if (!entry.indexBuffer) {
      cc_glglue_glGenBuffers(this->glue, 1, &entry.indexBuffer);
    }
    cc_glglue_glBindBuffer(this->glue, GL_ELEMENT_ARRAY_BUFFER,
                           entry.indexBuffer);
    cc_glglue_glBufferData(this->glue, GL_ELEMENT_ARRAY_BUFFER,
                           static_cast<GLsizeiptr>(geometry.indexCount) *
                           sizeof(uint32_t), geometry.indices, GL_STATIC_DRAW);
  }
  else if (entry.indexBuffer) {
    cc_glglue_glDeleteBuffers(this->glue, 1, &entry.indexBuffer);
    entry.indexBuffer = 0;
  }
}

void
SoGLRenderBackend::uploadLineDistanceBuffer(CachedCommand & entry,
                                            const SoGeometryDesc & geometry,
                                            const GLsizei vertexStride)
{
  const bool lineGeometry = geometry.topology == SO_TOPOLOGY_LINES ||
    geometry.topology == SO_TOPOLOGY_LINE_STRIP;
  if (!lineGeometry || !geometry.vertexCount) {
    if (entry.lineDistanceBuffer) {
      cc_glglue_glDeleteBuffers(this->glue, 1, &entry.lineDistanceBuffer);
      entry.lineDistanceBuffer = 0;
      entry.lineDistanceKey = nullptr;
    }
    return;
  }
  if (!entry.lineDistanceBuffer) {
    cc_glglue_glGenBuffers(this->glue, 1, &entry.lineDistanceBuffer);
  }
  std::vector<float> distances(geometry.vertexCount, 0.0f);
  const uint32_t strideFloats = static_cast<uint32_t>(vertexStride) /
    sizeof(float);
  const uint32_t count = geometry.indexCount && geometry.indices
    ? geometry.indexCount : geometry.vertexCount;
  if (geometry.topology == SO_TOPOLOGY_LINE_STRIP) {
    for (uint32_t i = 1; i < count; ++i) {
      const uint32_t previous = geometry.indices ? geometry.indices[i - 1] : i - 1;
      const uint32_t current = geometry.indices ? geometry.indices[i] : i;
      const float * p0 = geometry.positions + previous * strideFloats;
      const float * p1 = geometry.positions + current * strideFloats;
      const float dx = p1[0] - p0[0];
      const float dy = p1[1] - p0[1];
      const float dz = p1[2] - p0[2];
      distances[current] = distances[previous] +
        std::sqrt(dx * dx + dy * dy + dz * dz);
    }
  }
  else {
    for (uint32_t i = 0; i + 1 < count; i += 2) {
      const uint32_t first = geometry.indices ? geometry.indices[i] : i;
      const uint32_t second = geometry.indices ? geometry.indices[i + 1] : i + 1;
      const float * p0 = geometry.positions + first * strideFloats;
      const float * p1 = geometry.positions + second * strideFloats;
      const float dx = p1[0] - p0[0];
      const float dy = p1[1] - p0[1];
      const float dz = p1[2] - p0[2];
      distances[first] = 0.0f;
      distances[second] = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
  }
  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER,
                         entry.lineDistanceBuffer);
  cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER,
                         distances.size() * sizeof(float), distances.data(),
                         GL_STATIC_DRAW);
  entry.lineDistanceKey = geometry.positions;
}

void
SoGLRenderBackend::updateCacheDescription(CachedCommand & entry,
                                          const SoRenderCommand & command,
                                          const GLsizei vertexStride)
{
  const SoGeometryDesc & geometry = command.geometry;
  const SoTextureData & texture = command.material.texture;
  const bool hasTexture = texture.pixels && texture.width > 0 &&
    texture.height > 0 && texture.numComponents >= 1 &&
    texture.numComponents <= 4 && geometry.texcoords && geometry.vertexCount;
  entry.positionsKey = geometry.positions;
  entry.normalsKey = geometry.normals;
  entry.colorsKey = geometry.colors;
  entry.texcoordsKey = geometry.texcoords;
  entry.texturePixelsKey = hasTexture ? texture.pixels : nullptr;
  entry.indicesKey = geometry.indices;
  entry.vertexCount = geometry.vertexCount;
  entry.normalCount = geometry.normalCount;
  entry.indexCount = geometry.indexCount;
  entry.vertexStride = static_cast<uint32_t>(vertexStride);
  entry.texcoordStride = geometry.texcoordStride;
  entry.textureWidth = hasTexture ? texture.width : 0;
  entry.textureHeight = hasTexture ? texture.height : 0;
  entry.textureComponents = hasTexture ? texture.numComponents : 0;
  entry.textureColorSpace = hasTexture
    ? texture.colorSpace : SO_TEXTURE_COLORSPACE_LEGACY;
  entry.geometryCacheKey = geometry.cacheKey;
  entry.geometryRevision = geometry.revision;
  entry.textureCacheKey = hasTexture ? texture.cacheKey : 0;
  entry.textureRevision = hasTexture ? texture.revision : 0;
    entry.textureMinFilter = hasTexture ? texture.minFilter
                                      : SO_TEXTURE_FILTER_NEAREST;
  entry.textureMagFilter = hasTexture ? texture.magFilter
                                      : SO_TEXTURE_FILTER_NEAREST;
  entry.textureWrapS = hasTexture ? texture.wrapS
                                  : SO_TEXTURE_WRAP_CLAMP_TO_EDGE;
  entry.textureWrapT = hasTexture ? texture.wrapT
                                  : SO_TEXTURE_WRAP_CLAMP_TO_EDGE;
    entry.textureAnisotropic = hasTexture ? texture.anisotropic : false;
}

void
SoGLRenderBackend::uploadGeometry(CachedCommand & entry,
                                  const SoRenderCommand & command)
{
  const SoGeometryDesc & geometry = command.geometry;
  const GLsizei vertexStride = static_cast<GLsizei>(
    geometry.vertexStride ? geometry.vertexStride : sizeof(float) * 3);

  this->uploadVertexBuffers(entry, geometry);

  this->uploadTexture(entry, command);

  this->uploadLineDistanceBuffer(entry, geometry, vertexStride);

  this->uploadIndices(entry, geometry);

  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, 0);
  cc_glglue_glBindBuffer(this->glue, GL_ELEMENT_ARRAY_BUFFER, 0);

  this->updateCacheDescription(entry, command, vertexStride);
}

void
SoGLRenderBackend::setupVisualVAO(CachedCommand & entry)
{
  if (!entry.vertexArray) {
    this->glue->glGenVertexArrays(1, &entry.vertexArray);
  }
  this->glue->glBindVertexArray(entry.vertexArray);

  if (entry.positionBuffer) {
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER,
                           entry.positionBuffer);
    cc_glglue_glEnableVertexAttribArray(this->glue, POSITION_ATTRIBUTE);
    cc_glglue_glVertexAttribPointer(this->glue, POSITION_ATTRIBUTE, 3,
                                    GL_FLOAT,
                                    GL_FALSE, entry.vertexStride, nullptr);
  }
  if (entry.normalBuffer) {
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER,
                           entry.normalBuffer);
    cc_glglue_glEnableVertexAttribArray(this->glue, NORMAL_ATTRIBUTE);
    cc_glglue_glVertexAttribPointer(this->glue, NORMAL_ATTRIBUTE, 3,
                                    GL_FLOAT, GL_FALSE, entry.vertexStride,
                                    nullptr);
  }
  else {
    cc_glglue_glDisableVertexAttribArray(this->glue, NORMAL_ATTRIBUTE);
    this->glue->glVertexAttrib3f(NORMAL_ATTRIBUTE, 0.0f, 0.0f, 1.0f);
  }
  if (entry.colorBuffer) {
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.colorBuffer);
    cc_glglue_glEnableVertexAttribArray(this->glue, COLOR_ATTRIBUTE);
    cc_glglue_glVertexAttribPointer(this->glue, COLOR_ATTRIBUTE, 4, GL_FLOAT,
                                    GL_FALSE, 0, nullptr);
  }
  else {
    cc_glglue_glDisableVertexAttribArray(this->glue, COLOR_ATTRIBUTE);
    cc_glglue_glVertexAttrib4f(this->glue, COLOR_ATTRIBUTE,
                               1.0f, 1.0f, 1.0f, 1.0f);
  }
  if (entry.texcoordBuffer) {
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER,
                           entry.texcoordBuffer);
    cc_glglue_glEnableVertexAttribArray(this->glue, TEXCOORD_ATTRIBUTE);
    cc_glglue_glVertexAttribPointer(this->glue, TEXCOORD_ATTRIBUTE, 2,
                                    GL_FLOAT, GL_FALSE, 0, nullptr);
  }
  else {
    cc_glglue_glDisableVertexAttribArray(this->glue, TEXCOORD_ATTRIBUTE);
    cc_glglue_glVertexAttrib2f(this->glue, TEXCOORD_ATTRIBUTE, 0.0f, 0.0f);
  }
  if (entry.lineDistanceBuffer) {
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER,
                           entry.lineDistanceBuffer);
    cc_glglue_glEnableVertexAttribArray(this->glue, LINE_DISTANCE_ATTRIBUTE);
    cc_glglue_glVertexAttribPointer(this->glue, LINE_DISTANCE_ATTRIBUTE, 1,
                                    GL_FLOAT, GL_FALSE, 0, nullptr);
  }
  else {
    cc_glglue_glDisableVertexAttribArray(this->glue, LINE_DISTANCE_ATTRIBUTE);
    this->glue->glVertexAttrib1f(LINE_DISTANCE_ATTRIBUTE, 0.0f);
  }
  if (entry.indexBuffer) {
    cc_glglue_glBindBuffer(this->glue, GL_ELEMENT_ARRAY_BUFFER,
                           entry.indexBuffer);
  }
  this->glue->glBindVertexArray(0);
  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, 0);
  cc_glglue_glBindBuffer(this->glue, GL_ELEMENT_ARRAY_BUFFER, 0);
}

void
SoGLRenderBackend::setupLineRasterVAO(CachedCommand & entry)
{
  if (!entry.lineRasterVertexArray) {
    this->glue->glGenVertexArrays(1, &entry.lineRasterVertexArray);
  }
  this->glue->glBindVertexArray(entry.lineRasterVertexArray);

  auto bindAttribute = [this](GLuint buffer, GLuint attribute, GLint size) {
    if (buffer) {
      cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, buffer);
      cc_glglue_glEnableVertexAttribArray(this->glue, attribute);
      cc_glglue_glVertexAttribPointer(this->glue, attribute, size, GL_FLOAT,
                                      GL_FALSE, 0, nullptr);
    }
  };
  bindAttribute(entry.lineRasterPositionBuffer, POSITION_ATTRIBUTE, 3);
  if (!entry.lineRasterNormalBuffer) {
    cc_glglue_glDisableVertexAttribArray(this->glue, NORMAL_ATTRIBUTE);
    this->glue->glVertexAttrib3f(NORMAL_ATTRIBUTE, 0.0f, 0.0f, 1.0f);
  }
  else {
    bindAttribute(entry.lineRasterNormalBuffer, NORMAL_ATTRIBUTE, 3);
  }
  if (!entry.lineRasterColorBuffer) {
    cc_glglue_glDisableVertexAttribArray(this->glue, COLOR_ATTRIBUTE);
    cc_glglue_glVertexAttrib4f(this->glue, COLOR_ATTRIBUTE,
                               1.0f, 1.0f, 1.0f, 1.0f);
  }
  else {
    bindAttribute(entry.lineRasterColorBuffer, COLOR_ATTRIBUTE, 4);
  }
  if (!entry.lineRasterTexcoordBuffer) {
    cc_glglue_glDisableVertexAttribArray(this->glue, TEXCOORD_ATTRIBUTE);
    cc_glglue_glVertexAttrib2f(this->glue, TEXCOORD_ATTRIBUTE, 0.0f, 0.0f);
  }
  else {
    bindAttribute(entry.lineRasterTexcoordBuffer, TEXCOORD_ATTRIBUTE, 2);
  }
  bindAttribute(entry.lineRasterDistanceBuffer, LINE_DISTANCE_ATTRIBUTE, 1);

  this->glue->glBindVertexArray(0);
  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, 0);
}

void
SoGLRenderBackend::updateIndexedLineRasterStream(
  CachedCommand & entry,
  const SoRenderCommand & command,
  const SbMat & viewMat,
  const SbMat & projMat,
  const SbVec2s & viewportSize)
{
  const SoGeometryDesc & geometry = command.geometry;
  const uint32_t occurrenceCount = geometry.indexCount;
  if (!geometry.indices || occurrenceCount == 0 ||
      !geometry.positions || geometry.vertexCount == 0) {
    return;
  }

  const bool hasNormals = geometry.normals &&
    geometry.normalCount >= geometry.vertexCount;
  const bool hasColors = geometry.colors != nullptr;
  const bool hasTexcoords = geometry.texcoords != nullptr;
  const bool streamMatches = entry.lineRasterVertexArray != 0 &&
    entry.lineRasterPositionsKey == geometry.positions &&
    entry.lineRasterNormalsKey == geometry.normals &&
    entry.lineRasterColorsKey == geometry.colors &&
    entry.lineRasterTexcoordsKey == geometry.texcoords &&
    entry.lineRasterIndicesKey == geometry.indices &&
    entry.lineRasterIndexCount == occurrenceCount;

  if (!streamMatches) {
    this->destroyLineRasterStream(entry);
    this->glue->glGenBuffers(1, &entry.lineRasterPositionBuffer);
    if (hasNormals) this->glue->glGenBuffers(1, &entry.lineRasterNormalBuffer);
    if (hasColors) this->glue->glGenBuffers(1, &entry.lineRasterColorBuffer);
    if (hasTexcoords) {
      this->glue->glGenBuffers(1, &entry.lineRasterTexcoordBuffer);
    }
    this->glue->glGenBuffers(1, &entry.lineRasterDistanceBuffer);

    const uint32_t positionStride = geometry.vertexStride
      ? geometry.vertexStride / sizeof(float) : 3;
    const uint32_t texcoordStride = geometry.texcoordStride
      ? geometry.texcoordStride / sizeof(float) : 4;
    std::vector<float> positions(static_cast<size_t>(occurrenceCount) * 3);
    std::vector<float> normals;
    std::vector<float> colors;
    std::vector<float> texcoords;
    if (hasNormals) normals.resize(static_cast<size_t>(occurrenceCount) * 3);
    if (hasColors) colors.resize(static_cast<size_t>(occurrenceCount) * 4);
    if (hasTexcoords) texcoords.resize(static_cast<size_t>(occurrenceCount) * 2);

    for (uint32_t occurrence = 0; occurrence < occurrenceCount; ++occurrence) {
      const uint32_t source = geometry.indices[occurrence] < geometry.vertexCount
        ? geometry.indices[occurrence] : 0;
      const float * position = geometry.positions +
        static_cast<size_t>(source) * positionStride;
      std::copy(position, position + 3, positions.begin() +
                static_cast<size_t>(occurrence) * 3);
      if (hasNormals) {
        const float * normal = geometry.normals +
          static_cast<size_t>(source) * positionStride;
        std::copy(normal, normal + 3, normals.begin() +
                  static_cast<size_t>(occurrence) * 3);
      }
      if (hasColors) {
        const float * color = geometry.colors + static_cast<size_t>(source) * 4;
        std::copy(color, color + 4, colors.begin() +
                  static_cast<size_t>(occurrence) * 4);
      }
      if (hasTexcoords) {
        const float * texcoord = geometry.texcoords +
          static_cast<size_t>(source) * texcoordStride;
        texcoords[static_cast<size_t>(occurrence) * 2] = texcoord[0];
        texcoords[static_cast<size_t>(occurrence) * 2 + 1] = texcoord[1];
      }
    }

    auto upload = [this](GLuint buffer, const std::vector<float> & values) {
      if (!buffer) return;
      cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, buffer);
      cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER,
                             values.size() * sizeof(float), values.data(),
                             GL_STATIC_DRAW);
    };
    upload(entry.lineRasterPositionBuffer, positions);
    upload(entry.lineRasterNormalBuffer, normals);
    upload(entry.lineRasterColorBuffer, colors);
    upload(entry.lineRasterTexcoordBuffer, texcoords);
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, 0);

    entry.lineRasterPositionsKey = geometry.positions;
    entry.lineRasterNormalsKey = geometry.normals;
    entry.lineRasterColorsKey = geometry.colors;
    entry.lineRasterTexcoordsKey = geometry.texcoords;
    entry.lineRasterIndicesKey = geometry.indices;
    entry.lineRasterIndexCount = occurrenceCount;
    entry.lineRasterVertexCount = occurrenceCount;
    this->setupLineRasterVAO(entry);
  }

  const SbMatrix view(viewMat);
  const SbMatrix projection(projMat);
  const SbMatrix model(command.modelMatrix);
  const uint32_t positionStride = geometry.vertexStride
    ? geometry.vertexStride / sizeof(float) : 3;
  std::vector<SbVec2f> windowPositions(geometry.vertexCount);
  for (uint32_t i = 0; i < geometry.vertexCount; ++i) {
    const float * p = geometry.positions + static_cast<size_t>(i) * positionStride;
    SbVec3f point(p[0], p[1], p[2]);
    SbVec3f transformed;
    model.multVecMatrix(point, transformed);
    view.multVecMatrix(transformed, transformed);
    projection.multVecMatrix(transformed, transformed);
    windowPositions[i].setValue(
      (transformed[0] * 0.5f + 0.5f) * viewportSize[0],
      (transformed[1] * 0.5f + 0.5f) * viewportSize[1]);
  }

  std::vector<float> distances(occurrenceCount, 0.0f);
  auto sourceAt = [&geometry](uint32_t occurrence) {
    return geometry.indices[occurrence] < geometry.vertexCount
      ? geometry.indices[occurrence] : 0;
  };
  auto segmentLength = [&windowPositions](uint32_t first, uint32_t second) {
    return (windowPositions[second] - windowPositions[first]).length();
  };
  if (geometry.topology == SO_TOPOLOGY_LINE_STRIP) {
    for (uint32_t i = 1; i < occurrenceCount; ++i) {
      distances[i] = distances[i - 1] +
        segmentLength(sourceAt(i - 1), sourceAt(i));
    }
  }
  else {
    for (uint32_t i = 0; i + 1 < occurrenceCount; i += 2) {
      distances[i + 1] = segmentLength(sourceAt(i), sourceAt(i + 1));
    }
  }
  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER,
                         entry.lineRasterDistanceBuffer);
  cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER,
                         distances.size() * sizeof(float), distances.data(),
                         GL_DYNAMIC_DRAW);
  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, 0);
}

void
SoGLRenderBackend::updateLineDistances(CachedCommand & entry,
                                        const SoRenderCommand & command,
                                        const SbMat & viewMat,
                                        const SbMat & projMat,
                                        const SbVec2s & viewportSize)
{
  if (command.geometry.indices && command.geometry.indexCount &&
      (command.geometry.topology == SO_TOPOLOGY_LINES ||
       command.geometry.topology == SO_TOPOLOGY_LINE_STRIP)) {
    this->updateIndexedLineRasterStream(entry, command, viewMat, projMat,
                                        viewportSize);
    return;
  }
  if (!entry.lineDistanceBuffer || !command.geometry.positions ||
      command.geometry.vertexCount == 0) return;

  const SbMatrix view(viewMat);
  const SbMatrix projection(projMat);
  SbMatrix model(command.modelMatrix);
  const uint32_t strideFloats = static_cast<uint32_t>(
    command.geometry.vertexStride ? command.geometry.vertexStride
                                  : sizeof(float) * 3) / sizeof(float);
  const uint32_t count = command.geometry.indexCount && command.geometry.indices
    ? command.geometry.indexCount : command.geometry.vertexCount;
  std::vector<SbVec2f> windowPositions(command.geometry.vertexCount);
  for (uint32_t i = 0; i < command.geometry.vertexCount; ++i) {
    const float * p = command.geometry.positions + i * strideFloats;
    SbVec3f point(p[0], p[1], p[2]);
    SbVec3f transformed;
    model.multVecMatrix(point, transformed);
    view.multVecMatrix(transformed, transformed);
    projection.multVecMatrix(transformed, transformed);
    windowPositions[i].setValue(
      (transformed[0] * 0.5f + 0.5f) * viewportSize[0],
      (transformed[1] * 0.5f + 0.5f) * viewportSize[1]);
  }

  std::vector<float> distances(command.geometry.vertexCount, 0.0f);
  auto indexAt = [&command](uint32_t i) {
    return command.geometry.indices ? command.geometry.indices[i] : i;
  };
  auto segmentLength = [&windowPositions](uint32_t first, uint32_t second) {
    return (windowPositions[second] - windowPositions[first]).length();
  };
  if (command.geometry.topology == SO_TOPOLOGY_LINE_STRIP) {
    for (uint32_t i = 1; i < count; ++i) {
      const uint32_t previous = indexAt(i - 1);
      const uint32_t current = indexAt(i);
      distances[current] = distances[previous] +
        segmentLength(previous, current);
    }
  }
  else {
    for (uint32_t i = 0; i + 1 < count; i += 2) {
      const uint32_t first = indexAt(i);
      const uint32_t second = indexAt(i + 1);
      distances[first] = 0.0f;
      distances[second] = segmentLength(first, second);
    }
  }
  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER,
                         entry.lineDistanceBuffer);
  cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER,
                         distances.size() * sizeof(float), distances.data(),
                         GL_DYNAMIC_DRAW);
  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, 0);
}

bool
SoGLRenderBackend::textureDescriptionMatches(
  const CachedCommand & entry,
  const SoRenderCommand & command) const
{
  const SoTextureData & texture = command.material.texture;
  const bool identityMatches = texture.cacheKey != 0
    ? entry.textureCacheKey == texture.cacheKey &&
      entry.textureRevision == texture.revision
    : entry.texturePixelsKey == texture.pixels;
  return identityMatches &&
    entry.textureWidth == texture.width &&
    entry.textureHeight == texture.height &&
    entry.textureComponents == texture.numComponents &&
    entry.textureColorSpace == texture.colorSpace &&
    entry.textureMinFilter == texture.minFilter &&
    entry.textureMagFilter == texture.magFilter &&
    entry.textureWrapS == texture.wrapS &&
    entry.textureWrapT == texture.wrapT &&
    entry.textureAnisotropic == texture.anisotropic;
}

void
SoGLRenderBackend::updateGeometryCache(const SoDrawList & drawlist)
{
  const uint32_t generation = drawlist.getGeneration();
  if (!this->haveCacheGeneration || this->cacheGeneration != generation) {
    for (CachedCommand & entry : this->gpuCache) {
      if (!entry.persistent) this->destroyCacheEntry(entry);
    }
    std::vector<CachedCommand> persistentEntries;
    persistentEntries.reserve(this->gpuCache.size());
    for (CachedCommand & entry : this->gpuCache) {
      if (entry.persistent) persistentEntries.push_back(std::move(entry));
    }
    this->gpuCache.swap(persistentEntries);
    this->resourceToCache.clear();
    for (size_t i = 0; i < this->gpuCache.size(); ++i) {
      this->resourceToCache[this->gpuCache[i].resourceKey] = i;
    }
    this->commandToCache.clear();
  }
  this->cacheGeneration = generation;
  this->haveCacheGeneration = true;
  this->cachedCommandCount = static_cast<size_t>(drawlist.getNumCommands());

  for (int i = 0; i < drawlist.getNumCommands(); ++i) {
    const SoRenderCommand & command = drawlist.getCommand(i);
    const SoGeometryDesc & geometry = command.geometry;
    if ((!geometry.positions && geometry.cacheKey == 0) || geometry.vertexCount == 0 ||
        geometry.vertexCount > MAX_VERTEX_COUNT) continue;

    CachedCommand & entry = this->getOrCreateCache(&command);
    const uint32_t vertexStride = geometry.vertexStride
      ? geometry.vertexStride : sizeof(float) * 3;
    const bool identityMatches = geometry.cacheKey != 0
      ? entry.geometryCacheKey == geometry.cacheKey &&
        entry.geometryRevision == geometry.revision
      : entry.positionsKey == geometry.positions &&
        entry.normalsKey == geometry.normals;
    const bool lineGeometry = geometry.topology == SO_TOPOLOGY_LINES ||
      geometry.topology == SO_TOPOLOGY_LINE_STRIP;
    const bool lineDistanceMatches = geometry.cacheKey != 0
      ? entry.lineDistanceBuffer != 0 || !lineGeometry
      : entry.lineDistanceKey == (lineGeometry ? geometry.positions : nullptr);
    const bool geometryMatches = entry.positionBuffer != 0 &&
      identityMatches &&
      entry.colorsKey == geometry.colors &&
      entry.texcoordsKey == geometry.texcoords &&
      entry.indicesKey == geometry.indices &&
      entry.vertexCount == geometry.vertexCount &&
      entry.normalCount == geometry.normalCount &&
      entry.indexCount == geometry.indexCount &&
      entry.vertexStride == vertexStride &&
      entry.texcoordStride == geometry.texcoordStride &&
      lineDistanceMatches &&
      this->textureDescriptionMatches(entry, command) &&
      ((entry.texturePixelsKey != nullptr) ==
       (command.material.texture.pixels != nullptr));
    if (!geometryMatches) {
      if (!geometry.positions) continue;
      this->uploadGeometry(entry, command);
      this->setupVisualVAO(entry);
    }
  }
}

void
SoGLRenderBackend::uploadLighting(const SoDrawList & drawlist,
                                  const SoRenderCommand & command,
                                  const SurfaceUniforms & uniforms)
{
  const SoLightingData * lighting = drawlist.getLighting(command.lightingHandle);
  static const SoLightingData emptyLighting;
  if (!lighting) {
    lighting = &emptyLighting;
    if (command.lightingHandle != 0) {
      static std::once_flag invalidHandleWarning;
      std::call_once(invalidHandleWarning, []() {
        SoDebugError::postWarning(
          "SoGLRenderBackend::uploadLighting",
          "Draw command references missing lighting data; no headlight is "
          "synthesized.");
      });
    }
  }

  const SbVec3f & ambient = lighting->ambient;
  this->glue->glUniform3f(uniforms.lighting.ambient,
                          ambient[0], ambient[1], ambient[2]);

  GLint types[MAX_SHADER_LIGHTS] = {};
  GLfloat colors[MAX_SHADER_LIGHTS * 3] = {};
  GLfloat directions[MAX_SHADER_LIGHTS * 3] = {};
  GLfloat positions[MAX_SHADER_LIGHTS * 3] = {};
  GLfloat attenuations[MAX_SHADER_LIGHTS * 3] = {};
  GLfloat spotParams[MAX_SHADER_LIGHTS * 2] = {};
  const int count = std::min<int>(static_cast<int>(lighting->lights.size()),
                                  MAX_SHADER_LIGHTS);
  if (static_cast<int>(lighting->lights.size()) > MAX_SHADER_LIGHTS) {
    static std::once_flag lightLimitWarning;
    std::call_once(lightLimitWarning, []() {
      SoDebugError::postWarning(
        "SoGLRenderBackend::uploadLighting",
        "The Visual program supports eight lights; additional retained "
        "lights are ignored by this executor.");
    });
  }
  for (int i = 0; i < count; ++i) {
    const SoLightData & light = lighting->lights[static_cast<size_t>(i)];
    types[i] = static_cast<GLint>(light.type);
    colors[i * 3 + 0] = light.color[0];
    colors[i * 3 + 1] = light.color[1];
    colors[i * 3 + 2] = light.color[2];
    directions[i * 3 + 0] = light.direction[0];
    directions[i * 3 + 1] = light.direction[1];
    directions[i * 3 + 2] = light.direction[2];
    positions[i * 3 + 0] = light.position[0];
    positions[i * 3 + 1] = light.position[1];
    positions[i * 3 + 2] = light.position[2];
    attenuations[i * 3 + 0] = light.attenuation[0];
    attenuations[i * 3 + 1] = light.attenuation[1];
    attenuations[i * 3 + 2] = light.attenuation[2];
    spotParams[i * 2 + 0] = light.spotCutoffCos;
    spotParams[i * 2 + 1] = light.spotExponent;
  }
  this->glue->glUniform1i(uniforms.lighting.lightCount, count);
  this->glue->glUniform1iv(uniforms.lighting.lightType, MAX_SHADER_LIGHTS, types);
  this->glue->glUniform3fv(uniforms.lighting.lightColor, MAX_SHADER_LIGHTS,
                           colors);
  this->glue->glUniform3fv(uniforms.lighting.lightDirection, MAX_SHADER_LIGHTS,
                           directions);
  this->glue->glUniform3fv(uniforms.lighting.lightPosition, MAX_SHADER_LIGHTS,
                           positions);
  this->glue->glUniform3fv(uniforms.lighting.lightAttenuation, MAX_SHADER_LIGHTS,
                           attenuations);
  this->glue->glUniform2fv(uniforms.lighting.lightSpotParams, MAX_SHADER_LIGHTS,
                           spotParams);
}

void
SoGLRenderBackend::bindRasterCommon(const SoDrawList & drawlist,
                                    const SoRenderCommand & command,
                                    const SbMat & viewMat,
                                    const SbMat & projMat,
                                    const SbVec4f & color,
                                    const bool useVertexColor,
                                    const bool textured,
                                    const SurfaceUniforms & uniforms)
{
  SbMat model;
  command.modelMatrix.getValue(model);
  this->glue->glUniformMatrix4fv(uniforms.transforms.view, 1, GL_FALSE,
                                 &viewMat[0][0]);
  this->glue->glUniformMatrix4fv(uniforms.transforms.projection, 1, GL_FALSE,
                                 &projMat[0][0]);
  this->glue->glUniformMatrix4fv(uniforms.transforms.model, 1, GL_FALSE,
                                 &model[0][0]);
  this->glue->glUniform4f(uniforms.material.color,
                          color[0], color[1], color[2], color[3]);
  this->glue->glUniform1f(uniforms.material.useVertexColor,
                          useVertexColor ? 1.0f : 0.0f);

  const SoShadingModel shadingModel = command.material.shadingModel;
  this->glue->glUniform1i(uniforms.material.shadingModel,
                          static_cast<GLint>(shadingModel));
  const SbVec4f & emissive = command.material.emissive;
  const SbVec4f & ambient = command.material.ambient;
  const SbVec4f & specular = command.material.specular;
  this->glue->glUniform3f(uniforms.material.emissiveColor,
                          emissive[0], emissive[1], emissive[2]);
  this->glue->glUniform3f(uniforms.material.ambient,
                          ambient[0], ambient[1], ambient[2]);
  this->glue->glUniform3f(uniforms.material.specular,
                          specular[0], specular[1], specular[2]);
  this->glue->glUniform1f(uniforms.material.shininess,
                          command.material.shininess);
  this->glue->glUniform1f(uniforms.material.twoSidedLighting,
                          command.material.twoSidedLighting ? 1.0f : 0.0f);
  this->glue->glUniform1f(uniforms.material.vertexColorAlphaIncludesOpacity,
                          command.material.vertexColorAlphaIncludesOpacity
                            ? 1.0f : 0.0f);
  this->glue->glUniform1f(uniforms.texture.alphaIncludesOpacity,
                          command.material.textureAlphaIncludesOpacity
                            ? 1.0f : 0.0f);
  const bool textureHasAlpha = command.material.texture.numComponents == 2 ||
    command.material.texture.numComponents == 4;
  this->glue->glUniform1f(uniforms.texture.hasAlpha,
                          textureHasAlpha ? 1.0f : 0.0f);
  this->glue->glUniform1f(uniforms.texture.enabled,
                          textured ? 1.0f : 0.0f);
  this->glue->glUniform1i(uniforms.texture.sampler, 0);
  this->glue->glUniform1i(uniforms.texture.model,
                          static_cast<GLint>(command.material.texture.model));
  const SbVec4f & textureBlend = command.material.texture.blendColor;
  this->glue->glUniform4f(uniforms.texture.blendColor,
                          textureBlend[0], textureBlend[1],
                          textureBlend[2], textureBlend[3]);
  this->glue->glUniform1i(
    uniforms.alphaTest.function,
    command.state.alphaTest.policy == SO_ALPHA_TEST_POLICY_NONE
      ? 0 : static_cast<GLint>(command.state.alphaTest.function));
  this->glue->glUniform1f(uniforms.alphaTest.reference,
                          command.state.alphaTest.reference);
  this->uploadLighting(drawlist, command, uniforms);
}

void
SoGLRenderBackend::bindPointShader(const SoRenderCommand & command,
                                   const SbMat & viewMat,
                                   const SbMat & projMat,
                                   const SbVec4f & color,
                                   const bool useVertexColor,
                                   const float pointSize,
                                   const SbVec2s & viewportSize,
                                   const bool triangleInput,
                                   const SoDrawList & drawlist,
                                   const bool textured)
{
  const GLuint program = triangleInput
    ? this->rasterPrograms.trianglePoint.handle
    : this->rasterPrograms.point.handle;
  const PointProgram & pointProgram = triangleInput
    ? this->rasterPrograms.trianglePoint
    : this->rasterPrograms.point;
  cc_glglue_glUseProgram(this->glue, program);
  this->bindRasterCommon(drawlist, command, viewMat, projMat, color,
                         useVertexColor, textured, pointProgram.surface);
  this->glue->glUniform1f(pointProgram.raster.pointSize, pointSize);
  this->glue->glUniform2f(pointProgram.raster.viewportSize,
                          static_cast<float>(viewportSize[0]),
                          static_cast<float>(viewportSize[1]));
  if (triangleInput) {
    this->glue->glUniform1f(
      pointProgram.raster.cullBackFaces,
      command.state.raster.cullBackFaces ? 1.0f : 0.0f);
    this->glue->glUniform1f(
      pointProgram.raster.frontFaceCCW,
      command.state.raster.frontFaceCCW ? 1.0f : 0.0f);
  }
}

void
SoGLRenderBackend::bindLineShader(const SoRenderCommand & command,
                                  const SbMat & viewMat,
                                  const SbMat & projMat,
                                   const SbVec4f & color,
                                   const bool useVertexColor,
                                   const float lineWidth,
                                   const SbVec2s & viewportSize,
                                   const bool triangleInput,
                                   const SoDrawList & drawlist,
                                   const bool textured)
{
  const GLuint program = triangleInput
    ? this->rasterPrograms.triangleLine.handle
    : this->rasterPrograms.line.handle;
  const LineProgram & lineProgram = triangleInput
    ? this->rasterPrograms.triangleLine
    : this->rasterPrograms.line;
  cc_glglue_glUseProgram(this->glue, program);
  this->bindRasterCommon(drawlist, command, viewMat, projMat, color,
                         useVertexColor, textured, lineProgram.surface);
  this->glue->glUniform1f(lineProgram.raster.lineWidth, lineWidth);
  this->glue->glUniform2f(lineProgram.raster.viewportSize,
                          static_cast<float>(viewportSize[0]),
                          static_cast<float>(viewportSize[1]));
  if (triangleInput) {
    this->glue->glUniform1f(
      lineProgram.raster.cullBackFaces,
      command.state.raster.cullBackFaces ? 1.0f : 0.0f);
    this->glue->glUniform1f(
      lineProgram.raster.frontFaceCCW,
      command.state.raster.frontFaceCCW ? 1.0f : 0.0f);
  }
  this->glue->glUniform1i(
    lineProgram.raster.stipplePattern,
    static_cast<GLint>(command.state.raster.linePattern));
  this->glue->glUniform1f(
    lineProgram.raster.stippleScale,
    static_cast<GLfloat>(std::max(1, static_cast<int>(
      command.state.raster.linePatternScale))));
}

void
SoGLRenderBackend::bindPixelShader(const SoRenderCommand & command,
                                   const SbMat & viewMat,
                                   const SbMat & projMat,
                                   const SbVec2s & viewportOrigin,
                                   const SbVec2s & viewportSize)
{
  const PixelProgram & pixel = this->rasterPrograms.pixel;
  cc_glglue_glUseProgram(this->glue, pixel.handle);
  SbMat model;
  command.modelMatrix.getValue(model);
  this->glue->glUniformMatrix4fv(pixel.uniforms.view, 1, GL_FALSE,
                                 &viewMat[0][0]);
  this->glue->glUniformMatrix4fv(pixel.uniforms.projection, 1, GL_FALSE,
                                 &projMat[0][0]);
  this->glue->glUniformMatrix4fv(pixel.uniforms.model, 1, GL_FALSE,
                                 &model[0][0]);

  const GLsizei stride = static_cast<GLsizei>(
    command.geometry.vertexStride ? command.geometry.vertexStride : sizeof(float) * 3);
  const char * raw = reinterpret_cast<const char *>(command.geometry.positions);
  SbVec3f center(0.0f, 0.0f, 0.0f);
  for (uint32_t i = 0; i < command.geometry.vertexCount; ++i) {
    const float * position = reinterpret_cast<const float *>(raw + i * stride);
    center += SbVec3f(position[0], position[1], position[2]);
  }
  if (command.geometry.vertexCount) {
    center /= static_cast<float>(command.geometry.vertexCount);
  }
  this->glue->glUniform3f(pixel.uniforms.quadCenter,
                          center[0], center[1], center[2]);
  this->glue->glUniform2f(pixel.uniforms.sourceSize,
                          static_cast<float>(command.material.texture.width),
                          static_cast<float>(command.material.texture.height));
  this->glue->glUniform2f(pixel.uniforms.rasterSize,
                          static_cast<float>(command.pixelRaster.width),
                          static_cast<float>(command.pixelRaster.height));
  this->glue->glUniform2f(pixel.uniforms.viewportOrigin,
                          static_cast<float>(viewportOrigin[0]),
                          static_cast<float>(viewportOrigin[1]));
  this->glue->glUniform2f(pixel.uniforms.viewportSize,
                          static_cast<float>(viewportSize[0]),
                          static_cast<float>(viewportSize[1]));
  this->glue->glUniform2f(pixel.uniforms.pixelOrigin,
                          static_cast<float>(command.pixelRaster.originX),
                          static_cast<float>(command.pixelRaster.originY));
  this->glue->glUniform1i(pixel.uniforms.texture, 0);
  this->glue->glUniform1i(
    pixel.uniforms.alphaTestFunction,
    command.state.alphaTest.policy == SO_ALPHA_TEST_POLICY_NONE
      ? 0 : static_cast<GLint>(command.state.alphaTest.function));
  this->glue->glUniform1f(pixel.uniforms.alphaTestReference,
                          command.state.alphaTest.reference);
}

void
SoGLRenderBackend::drawCommand(const SoDrawList & drawlist,
                               const SoRenderCommand & command,
                               const SbMat & viewMat,
                               const SbMat & projMat,
                               const SoRenderParams & params)
{
  if (!command.state.raster.visible) return;
  if ((!command.geometry.positions && command.geometry.cacheKey == 0) ||
      command.geometry.vertexCount == 0) return;
  const auto found = this->commandToCache.find(&command);
  if (found == this->commandToCache.end()) return;
  CachedCommand & entry = this->gpuCache[found->second];
  if (!entry.vertexArray) return;

  RasterPath path = this->selectRasterPath(entry, command, params);
  if (path.useLineShader &&
      (path.primitive == GL_LINES || path.primitive == GL_LINE_STRIP)) {
    this->updateLineDistances(entry, command, viewMat, projMat,
                              params.viewport.getViewportSizePixels());
    path.expandedLineStream = command.geometry.indices &&
      command.geometry.indexCount && entry.lineRasterVertexArray != 0;
  }

  applyViewport(params);
  const SbVec4f & color = command.material.diffuse;
  this->applyDepthState(command);
  this->applyRasterState(command, path);
  this->applyBlendState(command, color);
  GLenum polygonOffsetTarget = GL_POLYGON_OFFSET_FILL;
  const bool polygonOffset = this->applyPolygonOffset(
    command, path, polygonOffsetTarget);
  this->bindCommandProgram(drawlist, command, path, viewMat, projMat,
                            params, entry);
  this->drawGeometry(command, path, entry);
  this->restoreRasterState(path, polygonOffsetTarget, polygonOffset);
}

SoGLRenderBackend::RasterPath
SoGLRenderBackend::selectRasterPath(const CachedCommand & entry,
                                    const SoRenderCommand & command,
                                    const SoRenderParams & params) const
{
  RasterPath path;
  path.primitive = topologyToGL(command.geometry.topology);
  path.textured = entry.texture != 0 && entry.texcoordBuffer != 0;
  path.pixelRaster = path.textured && command.pixelRaster.enabled &&
    command.pixelRaster.width > 0 && command.pixelRaster.height > 0;
  const float dpr = params.devicePixelRatio > 0.0f
    ? params.devicePixelRatio : 1.0f;
  path.pointSize = std::max(1.0f, command.state.raster.pointSize) * dpr;
  path.lineWidth = std::max(1.0f, command.state.raster.lineWidth) * dpr;
  const SoRasterFillMode fillMode = command.state.raster.fillMode;
  const bool triangleTopology = path.primitive == GL_TRIANGLES ||
    path.primitive == GL_TRIANGLE_STRIP;
  const bool lineTopology = path.primitive == GL_LINES ||
    path.primitive == GL_LINE_STRIP;
  const bool pointTopology = path.primitive == GL_POINTS;
  path.linePrimitive = lineTopology ||
    (fillMode == SO_RASTER_LINES && triangleTopology);
  path.pointPrimitive = pointTopology ||
    (fillMode == SO_RASTER_POINTS && triangleTopology);
  path.filledPrimitive = !path.linePrimitive && !path.pointPrimitive;
  const bool lineEmulationRequired =
    path.lineWidth > this->rasterPrograms.nativeLineWidthMax ||
    command.state.raster.linePattern != 0xFFFF;
  path.usePointShader = !path.pixelRaster && path.pointPrimitive &&
    this->rasterPrograms.point.handle != 0 &&
    path.pointSize > this->rasterPrograms.nativePointSizeMax;
  path.useLineShader = !path.pixelRaster && path.linePrimitive &&
    this->rasterPrograms.line.handle != 0 && lineEmulationRequired;
  path.lineTriangleInput = path.useLineShader &&
    fillMode == SO_RASTER_LINES && triangleTopology;
  path.pointTriangleInput = path.usePointShader &&
    fillMode == SO_RASTER_POINTS && triangleTopology;
  path.expandedLineStream = path.useLineShader && lineTopology &&
    command.geometry.indices && command.geometry.indexCount &&
    entry.lineRasterVertexArray != 0;
  return path;
}

void
SoGLRenderBackend::applyDepthState(const SoRenderCommand & command)
{
  if (command.state.depth.enabled) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(depthFunctionToGL(command.state.depth.func));
  }
  else {
    glDisable(GL_DEPTH_TEST);
  }
  glDepthMask(command.state.depth.writeEnabled &&
              command.pass != SO_RENDERPASS_TRANSPARENT
                ? GL_TRUE : GL_FALSE);
  glDepthRange(command.state.depth.range[0], command.state.depth.range[1]);
}

void
SoGLRenderBackend::applyRasterState(const SoRenderCommand & command,
                                    const RasterPath & path)
{
  const bool triangleFallback = path.lineTriangleInput ||
    path.pointTriangleInput;
  if (command.state.raster.cullBackFaces && !triangleFallback) {
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
  }
  else {
    glDisable(GL_CULL_FACE);
  }
  glFrontFace(command.state.raster.frontFaceCCW ? GL_CCW : GL_CW);
  const bool triangleTopology = path.primitive == GL_TRIANGLES ||
    path.primitive == GL_TRIANGLE_STRIP;
  if (!path.useLineShader && command.state.raster.fillMode == SO_RASTER_LINES &&
      !path.pointTriangleInput && triangleTopology) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  }
  else if (!path.usePointShader &&
           command.state.raster.fillMode == SO_RASTER_POINTS &&
           !path.lineTriangleInput && triangleTopology) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
  }
  if (!path.usePointShader && path.pointPrimitive) glPointSize(path.pointSize);
  if (!path.useLineShader && path.linePrimitive) glLineWidth(path.lineWidth);
}

void
SoGLRenderBackend::applyBlendState(const SoRenderCommand & command,
                                   const SbVec4f & color)
{
  const bool blending = command.state.blend.enabled ||
    command.pass == SO_RENDERPASS_TRANSPARENT || color[3] < 0.999f;
  if (!blending) {
    glDisable(GL_BLEND);
    return;
  }
  glEnable(GL_BLEND);
  if (isDualSourceBlendFactor(command.state.blend.srcRGBFactor) ||
      isDualSourceBlendFactor(command.state.blend.dstRGBFactor) ||
      isDualSourceBlendFactor(command.state.blend.srcAlphaFactor) ||
      isDualSourceBlendFactor(command.state.blend.dstAlphaFactor)) {
    static std::once_flag dualSourceWarning;
    std::call_once(dualSourceWarning, []() {
      SoDebugError::postWarning(
        "SoGLRenderBackend::applyBlendState",
        "Dual-source blend factors are not supported by the Visual program; "
        "using primary-source factors for execution.");
    });
  }
  cc_glglue_glBlendFuncSeparate(
    this->glue, blendFactorToGL(command.state.blend.srcRGBFactor),
    blendFactorToGL(command.state.blend.dstRGBFactor),
    blendFactorToGL(command.state.blend.srcAlphaFactor),
    blendFactorToGL(command.state.blend.dstAlphaFactor));
  if (cc_glglue_has_blendequation(this->glue) &&
      command.state.blend.rgbEquation == command.state.blend.alphaEquation) {
    cc_glglue_glBlendEquation(
      this->glue, blendEquationToGL(command.state.blend.rgbEquation));
  }
}

bool
SoGLRenderBackend::applyPolygonOffset(const SoRenderCommand & command,
                                      const RasterPath & path,
                                      GLenum & target)
{
  const bool applies = (path.filledPrimitive &&
                        command.state.raster.polygonOffsetFilled) ||
    (path.linePrimitive && command.state.raster.polygonOffsetLines) ||
    (path.pointPrimitive && command.state.raster.polygonOffsetPoints);
  const bool enabled = applies &&
    (command.state.raster.polygonOffsetFactor != 0.0f ||
     command.state.raster.polygonOffsetUnits != 0.0f);
  if (!enabled) return false;
  target = (path.useLineShader || path.usePointShader || path.filledPrimitive)
    ? GL_POLYGON_OFFSET_FILL
    : (path.linePrimitive ? GL_POLYGON_OFFSET_LINE : GL_POLYGON_OFFSET_POINT);
  glEnable(target);
  glPolygonOffset(command.state.raster.polygonOffsetFactor,
                  command.state.raster.polygonOffsetUnits);
  return true;
}

void
SoGLRenderBackend::bindCommandProgram(const SoDrawList & drawlist,
                                      const SoRenderCommand & command,
                                      const RasterPath & path,
                                      const SbMat & viewMat,
                                      const SbMat & projMat,
                                      const SoRenderParams & params,
                                      const CachedCommand & entry)
{
  if (path.textured) {
    cc_glglue_glActiveTexture(this->glue, GL_TEXTURE0);
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, entry.texture);
  }
  const SbVec4f & color = command.material.diffuse;
  if (path.pixelRaster) {
    this->bindPixelShader(command, viewMat, projMat,
                          params.viewport.getViewportOriginPixels(),
                          params.viewport.getViewportSizePixels());
  }
  else if (path.usePointShader) {
    this->bindPointShader(command, viewMat, projMat, color,
                          entry.colorBuffer != 0, path.pointSize,
                          params.viewport.getViewportSizePixels(),
                          path.pointTriangleInput, drawlist, path.textured);
  }
  else if (path.useLineShader) {
    this->bindLineShader(command, viewMat, projMat, color,
                         entry.colorBuffer != 0, path.lineWidth,
                         params.viewport.getViewportSizePixels(),
                         path.lineTriangleInput, drawlist, path.textured);
  }
  else {
    const VisualProgram & program = this->selectSurfaceProgram(command);
    cc_glglue_glUseProgram(this->glue, program.handle);
    this->bindRasterCommon(drawlist, command, viewMat, projMat, color,
                           entry.colorBuffer != 0, path.textured,
                           program.surface);
  }
}

void
SoGLRenderBackend::drawGeometry(const SoRenderCommand & command,
                                const RasterPath & path,
                                const CachedCommand & entry)
{
  this->glue->glBindVertexArray(path.expandedLineStream
                                ? entry.lineRasterVertexArray
                                : entry.vertexArray);
  if (path.expandedLineStream) {
    cc_glglue_glDrawArrays(this->glue, path.primitive, 0,
                           static_cast<GLsizei>(entry.lineRasterVertexCount));
  }
  else if (command.geometry.indexCount && command.geometry.indices) {
    cc_glglue_glDrawElements(this->glue, path.primitive,
                             static_cast<GLsizei>(command.geometry.indexCount),
                             GL_UNSIGNED_INT, nullptr);
  }
  else {
    cc_glglue_glDrawArrays(this->glue, path.primitive, 0,
                           static_cast<GLsizei>(command.geometry.vertexCount));
  }
  this->glue->glBindVertexArray(0);
}

void
SoGLRenderBackend::restoreRasterState(const RasterPath & path,
                                      const GLenum polygonOffsetTarget,
                                      const bool polygonOffsetEnabled)
{
  if (path.pixelRaster || path.usePointShader || path.useLineShader) {
    cc_glglue_glUseProgram(this->glue, this->visualProgram.handle);
  }
  if (path.textured) cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, 0);
  if (polygonOffsetEnabled) glDisable(polygonOffsetTarget);
  if (!path.filledPrimitive) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDepthRange(0.0, 1.0);
  glFrontFace(GL_CCW);
  if (!path.usePointShader) glPointSize(1.0f);
  if (!path.useLineShader) glLineWidth(1.0f);
}

void
SoGLRenderBackend::renderPass(const SoDrawList & drawlist,
                              const SbMat & viewMat,
                              const SbMat & projMat,
                              const SoRenderParams & params,
                              const SoRenderPassType pass)
{
  const std::vector<int> & order = drawlist.getSortedOrder();
  for (int i = 0; i < drawlist.getNumCommands(); ++i) {
    const int index = i < static_cast<int>(order.size()) ? order[i] : i;
    const SoRenderCommand & command = drawlist.getCommand(index);
    if (command.pass == pass) {
      this->drawCommand(drawlist, command, viewMat, projMat, params);
    }
  }
}

void
SoGLRenderBackend::beginFrame(const SoRenderParams & params)
{
  // Establish a deterministic baseline. These values are not interpretations
  // of retained Coin state; semantic depth/blend/raster execution is layered
  // above this executor.
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDisable(GL_POLYGON_OFFSET_LINE);
  glDisable(GL_POLYGON_OFFSET_POINT);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glPointSize(1.0f);
  glLineWidth(1.0f);

  if (params.flags & SO_PARAM_CLEAR_WINDOW) {
    const SbColor4f & color = params.clearColor;
    glClearColor(color[0], color[1], color[2], color[3]);
  }
  GLbitfield clearMask = 0;
  if (params.flags & SO_PARAM_CLEAR_WINDOW) clearMask |= GL_COLOR_BUFFER_BIT;
  if (params.flags & SO_PARAM_CLEAR_DEPTH) {
    glClearDepth(params.clearDepth);
    clearMask |= GL_DEPTH_BUFFER_BIT;
  }
  if (clearMask) glClear(clearMask);

  applyViewport(params);
  cc_glglue_glUseProgram(this->glue, this->visualProgram.handle);
}

const SoGLRenderBackend::VisualProgram &
SoGLRenderBackend::selectSurfaceProgram(const SoRenderCommand & command) const
{
  // Centralize the mapping from retained material semantics to an executor
  // implementation. Both current models use the retained Inventor lighting
  // evaluation program, with u_shadingModel selecting its defined behavior.
  switch (command.material.shadingModel) {
  case SO_SHADING_UNLIT:
  case SO_SHADING_LEGACY_GOURAUD:
  default:
    return this->visualProgram;
  }
}

bool
SoGLRenderBackend::createShaders()
{
  this->visualProgram.handle = linkProgram(
    this->glue, coin_gl_visual_vertex_shadersource,
    coin_gl_visual_fragment_shadersource);
  this->rasterPrograms.line.handle = linkProgram(
    this->glue, coin_gl_wide_line_vertex_shadersource,
    coin_gl_wide_line_fragment_shadersource,
    coin_gl_wide_line_geometry_shadersource);
  this->rasterPrograms.triangleLine.handle = linkProgram(
    this->glue, coin_gl_wide_line_vertex_shadersource,
    coin_gl_wide_line_fragment_shadersource,
    coin_gl_wide_line_triangle_geometry_shadersource);
  this->rasterPrograms.point.handle = linkProgram(
    this->glue, coin_gl_point_vertex_shadersource,
    coin_gl_point_fragment_shadersource,
    coin_gl_point_geometry_shadersource);
  this->rasterPrograms.trianglePoint.handle = linkProgram(
    this->glue, coin_gl_point_vertex_shadersource,
    coin_gl_point_fragment_shadersource,
    coin_gl_point_triangle_geometry_shadersource);
  this->rasterPrograms.pixel.handle = linkProgram(
    this->glue, coin_gl_pixel_vertex_shadersource,
    coin_gl_pixel_fragment_shadersource);

  const GLuint programs[] = {
    this->visualProgram.handle,
    this->rasterPrograms.line.handle,
    this->rasterPrograms.triangleLine.handle,
    this->rasterPrograms.point.handle,
    this->rasterPrograms.trianglePoint.handle,
    this->rasterPrograms.pixel.handle
  };
  for (const GLuint program : programs) {
    if (!program) {
      for (const GLuint created : programs) {
        if (created) cc_glglue_glDeleteProgram(this->glue, created);
      }
      this->visualProgram.handle = 0;
      this->rasterPrograms.line.handle = 0;
      this->rasterPrograms.triangleLine.handle = 0;
      this->rasterPrograms.point.handle = 0;
      this->rasterPrograms.trianglePoint.handle = 0;
      this->rasterPrograms.pixel.handle = 0;
      return false;
    }
  }

  auto uniform = [this](GLuint program, const char * name) {
    return cc_glglue_glGetUniformLocation(this->glue, program, name);
  };
  auto cacheSurface = [this, &uniform](SurfaceUniforms & surface,
                                       const GLuint program) {
    surface.transforms.view = uniform(program, "u_view");
    surface.transforms.projection = uniform(program, "u_proj");
    surface.transforms.model = uniform(program, "u_model");
    surface.material.color = uniform(program, "u_color");
    surface.material.useVertexColor = uniform(program, "u_useVertexColor");
    surface.material.shadingModel = uniform(program, "u_shadingModel");
    surface.material.emissiveColor = uniform(program, "u_emissiveColor");
    surface.material.ambient = uniform(program, "u_materialAmbient");
    surface.material.specular = uniform(program, "u_materialSpecular");
    surface.material.shininess = uniform(program, "u_materialShininess");
    surface.material.twoSidedLighting = uniform(program, "u_twoSidedLighting");
    surface.material.vertexColorAlphaIncludesOpacity =
      uniform(program, "u_vertexColorAlphaIncludesOpacity");
    surface.texture.alphaIncludesOpacity =
      uniform(program, "u_textureAlphaIncludesOpacity");
    surface.texture.hasAlpha = uniform(program, "u_textureHasAlpha");
    surface.lighting.ambient = uniform(program, "u_ambientLight");
    surface.lighting.lightCount = uniform(program, "u_lightCount");
    surface.lighting.lightType = uniform(program, "u_lightType");
    surface.lighting.lightColor = uniform(program, "u_lightColor");
    surface.lighting.lightDirection = uniform(program, "u_lightDirection");
    surface.lighting.lightPosition = uniform(program, "u_lightPosition");
    surface.lighting.lightAttenuation = uniform(program, "u_lightAttenuation");
    surface.lighting.lightSpotParams = uniform(program, "u_lightSpotParams");
    surface.texture.sampler = uniform(program, "u_texture");
    surface.texture.enabled = uniform(program, "u_textureEnabled");
    surface.texture.model = uniform(program, "u_textureModel");
    surface.texture.blendColor = uniform(program, "u_textureBlendColor");
    surface.alphaTest.function = uniform(program, "u_alphaTestFunction");
    surface.alphaTest.reference = uniform(program, "u_alphaTestReference");
  };
  cacheSurface(this->visualProgram.surface, this->visualProgram.handle);
  cacheSurface(this->rasterPrograms.line.surface,
               this->rasterPrograms.line.handle);
  cacheSurface(this->rasterPrograms.triangleLine.surface,
               this->rasterPrograms.triangleLine.handle);
  cacheSurface(this->rasterPrograms.point.surface,
               this->rasterPrograms.point.handle);
  cacheSurface(this->rasterPrograms.trianglePoint.surface,
               this->rasterPrograms.trianglePoint.handle);

  LineProgram::RasterUniforms & line = this->rasterPrograms.line.raster;
  line.lineWidth = uniform(this->rasterPrograms.line.handle, "u_lineWidth");
  line.viewportSize = uniform(this->rasterPrograms.line.handle, "u_vpSize");
  line.stipplePattern = uniform(this->rasterPrograms.line.handle,
                                "u_stipplePattern");
  line.stippleScale = uniform(this->rasterPrograms.line.handle,
                              "u_stippleScale");
  line.cullBackFaces = uniform(this->rasterPrograms.line.handle,
                               "u_cullBackFaces");
  line.frontFaceCCW = uniform(this->rasterPrograms.line.handle,
                              "u_frontFaceCCW");
  this->rasterPrograms.triangleLine.raster = line;
  this->rasterPrograms.triangleLine.raster.lineWidth =
    uniform(this->rasterPrograms.triangleLine.handle, "u_lineWidth");
  this->rasterPrograms.triangleLine.raster.viewportSize =
    uniform(this->rasterPrograms.triangleLine.handle, "u_vpSize");
  this->rasterPrograms.triangleLine.raster.stipplePattern =
    uniform(this->rasterPrograms.triangleLine.handle, "u_stipplePattern");
  this->rasterPrograms.triangleLine.raster.stippleScale =
    uniform(this->rasterPrograms.triangleLine.handle, "u_stippleScale");
  this->rasterPrograms.triangleLine.raster.cullBackFaces =
    uniform(this->rasterPrograms.triangleLine.handle, "u_cullBackFaces");
  this->rasterPrograms.triangleLine.raster.frontFaceCCW =
    uniform(this->rasterPrograms.triangleLine.handle, "u_frontFaceCCW");

  PointProgram::RasterUniforms & point = this->rasterPrograms.point.raster;
  point.pointSize = uniform(this->rasterPrograms.point.handle, "u_pointSize");
  point.viewportSize = uniform(this->rasterPrograms.point.handle, "u_vpSize");
  point.cullBackFaces = uniform(this->rasterPrograms.point.handle,
                                "u_cullBackFaces");
  point.frontFaceCCW = uniform(this->rasterPrograms.point.handle,
                               "u_frontFaceCCW");
  this->rasterPrograms.trianglePoint.raster = point;
  this->rasterPrograms.trianglePoint.raster.pointSize =
    uniform(this->rasterPrograms.trianglePoint.handle, "u_pointSize");
  this->rasterPrograms.trianglePoint.raster.viewportSize =
    uniform(this->rasterPrograms.trianglePoint.handle, "u_vpSize");
  this->rasterPrograms.trianglePoint.raster.cullBackFaces =
    uniform(this->rasterPrograms.trianglePoint.handle, "u_cullBackFaces");
  this->rasterPrograms.trianglePoint.raster.frontFaceCCW =
    uniform(this->rasterPrograms.trianglePoint.handle, "u_frontFaceCCW");

  PixelProgram::Uniforms & pixel = this->rasterPrograms.pixel.uniforms;
  pixel.view = uniform(this->rasterPrograms.pixel.handle, "u_view");
  pixel.projection = uniform(this->rasterPrograms.pixel.handle, "u_proj");
  pixel.model = uniform(this->rasterPrograms.pixel.handle, "u_model");
  pixel.quadCenter = uniform(this->rasterPrograms.pixel.handle, "u_quadCenter");
  pixel.sourceSize = uniform(this->rasterPrograms.pixel.handle,
                             "u_sourceSize");
  pixel.rasterSize = uniform(this->rasterPrograms.pixel.handle,
                             "u_rasterSize");
  pixel.viewportOrigin = uniform(this->rasterPrograms.pixel.handle,
                                 "u_viewportOrigin");
  pixel.viewportSize = uniform(this->rasterPrograms.pixel.handle, "u_vpSize");
  pixel.pixelOrigin = uniform(this->rasterPrograms.pixel.handle, "u_pixelOrigin");
  pixel.texture = uniform(this->rasterPrograms.pixel.handle, "u_texture");
  pixel.alphaTestFunction = uniform(this->rasterPrograms.pixel.handle,
                                    "u_alphaTestFunction");
  pixel.alphaTestReference = uniform(this->rasterPrograms.pixel.handle,
                                     "u_alphaTestReference");
  return true;
}

SbBool
SoGLRenderBackend::render(const SoDrawList & drawlist,
                          const SoRenderPlan & plan,
                          const SoRenderParams & params)
{
  if (!this->isInitialized()) {
    this->emitError("render called before backend initialization");
    return FALSE;
  }

  this->debugValidateDrawList(drawlist);
  this->beginFrame(params);
  this->updateGeometryCache(drawlist);

  SbMat view;
  SbMat projection;
  params.viewMatrix.getValue(view);
  params.projMatrix.getValue(projection);

  for (int i = 0; i < plan.getNumDraws(); ++i) {
    const uint32_t commandIndex = plan.getDraw(i).commandIndex;
    if (commandIndex >= static_cast<uint32_t>(drawlist.getNumCommands())) {
      this->emitError("render plan references a missing DrawList command");
      return FALSE;
    }
    this->drawCommand(drawlist, drawlist.getCommand(
      static_cast<int>(commandIndex)), view, projection, params);
  }
  cc_glglue_glUseProgram(this->glue, 0);
  return TRUE;
}
