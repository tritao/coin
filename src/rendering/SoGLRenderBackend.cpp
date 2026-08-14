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
#include <mutex>
#include <string>
#include <vector>

#include <data/shaders/gl/visual/Fragment.h>
#include <data/shaders/gl/visual/Vertex.h>

namespace {

static constexpr int MAX_VERTEX_COUNT = 10000000;
static constexpr int MAX_SHADER_LIGHTS = 8;
static constexpr GLuint POSITION_ATTRIBUTE = 0;
static constexpr GLuint NORMAL_ATTRIBUTE = 1;
static constexpr GLuint COLOR_ATTRIBUTE = 2;
static constexpr GLuint TEXCOORD_ATTRIBUTE = 3;

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
linkProgram(const cc_glglue * glue,
            const char * vertexSource,
            const char * fragmentSource)
{
  const GLuint vertex = compileShader(glue, GL_VERTEX_SHADER, vertexSource);
  const GLuint fragment = compileShader(glue, GL_FRAGMENT_SHADER,
                                         fragmentSource);
  if (!vertex || !fragment) {
    if (vertex) cc_glglue_glDeleteShader(glue, vertex);
    if (fragment) cc_glglue_glDeleteShader(glue, fragment);
    return 0;
  }

  const GLuint program = cc_glglue_glCreateProgram(glue);
  cc_glglue_glAttachShader(glue, program, vertex);
  cc_glglue_glAttachShader(glue, program, fragment);
  cc_glglue_glLinkProgram(glue, program);

  GLint linked = GL_FALSE;
  cc_glglue_glGetGLSLProgramiv(glue, program, GL_LINK_STATUS, &linked);
  if (linked == GL_FALSE) {
    GLint length = 0;
    cc_glglue_glGetGLSLProgramiv(glue, program, GL_INFO_LOG_LENGTH, &length);
    if (length > 0) {
      std::string log(static_cast<size_t>(length), '\0');
      cc_glglue_glGetProgramInfoLog(glue, program, length, &length, &log[0]);
      SoDebugError::postInfo("SoGLRenderBackend::linkProgram",
                             "%s", log.c_str());
    }
    cc_glglue_glDeleteProgram(glue, program);
  }

  cc_glglue_glDeleteShader(glue, vertex);
  cc_glglue_glDeleteShader(glue, fragment);
  return linked == GL_FALSE ? 0 : program;
}

struct TextureUploadFormat {
  GLint internalFormat;
  GLenum format;
  GLint swizzle[4];
};

TextureUploadFormat
textureUploadFormat(const int components)
{
  switch (components) {
  case 1:
    return { GL_R8, GL_RED, { GL_RED, GL_RED, GL_RED, GL_ONE } };
  case 2:
    return { GL_RG8, GL_RG, { GL_RED, GL_RED, GL_RED, GL_GREEN } };
  case 3:
    return { GL_RGB8, GL_RGB, { GL_RED, GL_GREEN, GL_BLUE, GL_ONE } };
  case 4:
  default:
    return { GL_RGBA8, GL_RGBA,
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
      !this->glue->glVertexAttribPointer ||
      !this->glue->glEnableVertexAttribArray ||
      !this->glue->glDisableVertexAttribArray ||
      !this->glue->glVertexAttrib4f ||
      !this->glue->glVertexAttrib3f ||
      !this->glue->glVertexAttrib2f ||
      !this->glue->glUniform1f || !this->glue->glUniform1i ||
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

  this->setInitialized(TRUE);
  return TRUE;
}

void
SoGLRenderBackend::destroyCacheEntry(CachedCommand & entry)
{
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
SoGLRenderBackend::invalidateCache()
{
  if (this->glue) {
    for (CachedCommand & entry : this->gpuCache) {
      this->destroyCacheEntry(entry);
    }
  }
  this->gpuCache.clear();
  this->commandToCache.clear();
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
    this->visualProgram = VisualProgram();
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

  const size_t index = this->gpuCache.size();
  this->gpuCache.emplace_back();
  this->commandToCache[command] = index;
  return this->gpuCache.back();
}

void
SoGLRenderBackend::uploadGeometry(CachedCommand & entry,
                                  const SoRenderCommand & command)
{
  const SoGeometryDesc & geometry = command.geometry;
  const uint32_t vertexStride = geometry.vertexStride
    ? geometry.vertexStride : sizeof(float) * 3;
  const SoTextureData & texture = command.material.texture;
  const bool hasTexture = texture.pixels && texture.width > 0 &&
    texture.height > 0 && texture.numComponents >= 1 &&
    texture.numComponents <= 4 && geometry.texcoords && geometry.vertexCount;

  this->uploadVertexBuffers(entry, geometry);
  if (hasTexture) this->uploadTexture(entry, geometry, texture);
  else {
    if (entry.texcoordBuffer) {
      cc_glglue_glDeleteBuffers(this->glue, 1, &entry.texcoordBuffer);
      entry.texcoordBuffer = 0;
    }
    if (entry.texture) {
      cc_glglue_glDeleteTextures(this->glue, 1, &entry.texture);
      entry.texture = 0;
    }
  }
  this->uploadIndices(entry, geometry);

  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, 0);
  cc_glglue_glBindBuffer(this->glue, GL_ELEMENT_ARRAY_BUFFER, 0);
  this->updateCacheDescription(entry, command, hasTexture, vertexStride);
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
                         vertexStride,
                         geometry.positions, GL_STATIC_DRAW);

  if (geometry.normals && geometry.normalCount >= geometry.vertexCount) {
    if (!entry.normalBuffer) {
      cc_glglue_glGenBuffers(this->glue, 1, &entry.normalBuffer);
    }
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.normalBuffer);
    cc_glglue_glBufferData(this->glue, GL_ARRAY_BUFFER,
                           static_cast<GLsizeiptr>(geometry.vertexCount) *
                           vertexStride,
                           geometry.normals, GL_STATIC_DRAW);
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
                           sizeof(float) * 4,
                           geometry.colors, GL_STATIC_DRAW);
  }
  else if (entry.colorBuffer) {
    cc_glglue_glDeleteBuffers(this->glue, 1, &entry.colorBuffer);
    entry.colorBuffer = 0;
  }
}

void
SoGLRenderBackend::uploadTexture(CachedCommand & entry,
                                 const SoGeometryDesc & geometry,
                                 const SoTextureData & texture)
{
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
                         texcoords.size() * sizeof(float),
                         texcoords.data(), GL_STATIC_DRAW);

  if (!entry.texture) cc_glglue_glGenTextures(this->glue, 1, &entry.texture);
  cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, entry.texture);

  const TextureUploadFormat format =
    textureUploadFormat(texture.numComponents);
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
                           sizeof(uint32_t),
                           geometry.indices, GL_STATIC_DRAW);
  }
  else if (entry.indexBuffer) {
    cc_glglue_glDeleteBuffers(this->glue, 1, &entry.indexBuffer);
    entry.indexBuffer = 0;
  }
}

void
SoGLRenderBackend::updateCacheDescription(CachedCommand & entry,
                                          const SoRenderCommand & command,
                                          const bool hasTexture,
                                          const uint32_t vertexStride)
{
  const SoGeometryDesc & geometry = command.geometry;
  const SoTextureData & texture = command.material.texture;
  entry.positionsKey = geometry.positions;
  entry.normalsKey = geometry.normals;
  entry.colorsKey = geometry.colors;
  entry.texcoordsKey = geometry.texcoords;
  entry.texturePixelsKey = hasTexture ? texture.pixels : nullptr;
  entry.indicesKey = geometry.indices;
  entry.vertexCount = geometry.vertexCount;
  entry.normalCount = geometry.normalCount;
  entry.indexCount = geometry.indexCount;
  entry.vertexStride = vertexStride;
  entry.texcoordStride = geometry.texcoordStride;
  entry.textureWidth = hasTexture ? texture.width : 0;
  entry.textureHeight = hasTexture ? texture.height : 0;
  entry.textureComponents = hasTexture ? texture.numComponents : 0;
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
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, entry.normalBuffer);
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
  if (entry.indexBuffer) {
    cc_glglue_glBindBuffer(this->glue, GL_ELEMENT_ARRAY_BUFFER,
                           entry.indexBuffer);
  }
  this->glue->glBindVertexArray(0);
  cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER, 0);
  cc_glglue_glBindBuffer(this->glue, GL_ELEMENT_ARRAY_BUFFER, 0);
}

bool
SoGLRenderBackend::textureDescriptionMatches(
  const CachedCommand & entry,
  const SoRenderCommand & command) const
{
  const SoTextureData & texture = command.material.texture;
  return entry.texturePixelsKey == texture.pixels &&
    entry.textureWidth == texture.width &&
    entry.textureHeight == texture.height &&
    entry.textureComponents == texture.numComponents &&
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
  if ((this->haveCacheGeneration && this->cacheGeneration != generation) ||
      (this->haveCacheGeneration &&
       this->cachedCommandCount != static_cast<size_t>(drawlist.getNumCommands()))) {
    this->invalidateCache();
  }
  this->cacheGeneration = generation;
  this->haveCacheGeneration = true;
  this->cachedCommandCount = static_cast<size_t>(drawlist.getNumCommands());

  for (int i = 0; i < drawlist.getNumCommands(); ++i) {
    const SoRenderCommand & command = drawlist.getCommand(i);
    const SoGeometryDesc & geometry = command.geometry;
    if (!geometry.positions || geometry.vertexCount == 0 ||
        geometry.vertexCount > MAX_VERTEX_COUNT) continue;

    CachedCommand & entry = this->getOrCreateCache(&command);
    const uint32_t vertexStride = geometry.vertexStride
      ? geometry.vertexStride : sizeof(float) * 3;
    const bool geometryMatches = entry.positionBuffer != 0 &&
      entry.cacheGeneration == generation &&
      entry.positionsKey == geometry.positions &&
      entry.normalsKey == geometry.normals &&
      entry.colorsKey == geometry.colors &&
      entry.texcoordsKey == geometry.texcoords &&
      entry.indicesKey == geometry.indices &&
      entry.vertexCount == geometry.vertexCount &&
      entry.normalCount == geometry.normalCount &&
      entry.indexCount == geometry.indexCount &&
      entry.vertexStride == vertexStride &&
      entry.texcoordStride == geometry.texcoordStride &&
      this->textureDescriptionMatches(entry, command) &&
      ((entry.texturePixelsKey != nullptr) ==
       (command.material.texture.pixels != nullptr));
    if (!geometryMatches) {
      this->uploadGeometry(entry, command);
      this->setupVisualVAO(entry);
      entry.cacheGeneration = generation;
    }
  }
}

void
SoGLRenderBackend::uploadLighting(const SoDrawList & drawlist,
                                  const SoRenderCommand & command)
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
          "Ignoring an invalid retained lighting handle; no headlight is synthesized.");
      });
    }
  }

  const SbVec3f & ambient = lighting->ambient;
  const VisualProgram::Uniforms & uniforms = this->visualProgram.uniforms;
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
        "The retained GL Visual program supports eight lights; additional "
        "retained lights are not uploaded.");
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
  this->glue->glUniform3fv(uniforms.lighting.lightColor, MAX_SHADER_LIGHTS, colors);
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
SoGLRenderBackend::drawCommand(const SoDrawList & drawlist,
                               const SoRenderCommand & command,
                                const SbMat & viewMat,
                                const SbMat & projMat,
                                const SoRenderParams & params)
{
  if (!command.geometry.positions || command.geometry.vertexCount == 0) return;
  const auto found = this->commandToCache.find(&command);
  if (found == this->commandToCache.end()) return;
  const CachedCommand & entry = this->gpuCache[found->second];
  if (!entry.vertexArray) return;

  this->bindVisualCommand(drawlist, command, entry, viewMat, projMat, params);
  this->drawGeometry(command, entry);
}

void
SoGLRenderBackend::bindTransforms(const SoRenderCommand & command,
                                  const SbMat & viewMat,
                                  const SbMat & projMat)
{
  const VisualProgram::Uniforms & uniforms = this->visualProgram.uniforms;
  this->glue->glUniformMatrix4fv(uniforms.transforms.view, 1, GL_FALSE,
                                 &viewMat[0][0]);
  this->glue->glUniformMatrix4fv(uniforms.transforms.projection, 1, GL_FALSE,
                                 &projMat[0][0]);
  SbMat model;
  command.modelMatrix.getValue(model);
  this->glue->glUniformMatrix4fv(uniforms.transforms.model, 1, GL_FALSE,
                                 &model[0][0]);

}

void
SoGLRenderBackend::bindMaterial(const SoRenderCommand & command,
                                const CachedCommand & entry)
{
  const VisualProgram::Uniforms & uniforms = this->visualProgram.uniforms;
  const SbVec4f & color = command.material.diffuse;
  this->glue->glUniform4f(uniforms.material.color,
                          color[0], color[1], color[2], color[3]);
  this->glue->glUniform1f(uniforms.material.useVertexColor,
                          entry.colorBuffer ? 1.0f : 0.0f);
  this->glue->glUniform1f(
    uniforms.material.vertexColorAlphaIncludesOpacity,
    command.material.vertexColorAlphaIncludesOpacity ? 1.0f : 0.0f);
  this->glue->glUniform1f(
    uniforms.texture.alphaIncludesOpacity,
    command.material.textureAlphaIncludesOpacity ? 1.0f : 0.0f);
  const bool textureHasAlpha = command.material.texture.numComponents == 2 ||
    command.material.texture.numComponents == 4;
  this->glue->glUniform1f(uniforms.texture.hasAlpha,
                          textureHasAlpha ? 1.0f : 0.0f);

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
SoGLRenderBackend::applyBlendState(const SoRenderCommand & command)
{
  const bool blending = command.state.blend.enabled ||
    command.pass == SO_RENDERPASS_TRANSPARENT ||
    command.material.diffuse[3] < 0.999f;
  if (blending) {
    glEnable(GL_BLEND);
    if (isDualSourceBlendFactor(command.state.blend.srcRGBFactor) ||
        isDualSourceBlendFactor(command.state.blend.dstRGBFactor) ||
        isDualSourceBlendFactor(command.state.blend.srcAlphaFactor) ||
        isDualSourceBlendFactor(command.state.blend.dstAlphaFactor)) {
      static std::once_flag dualSourceWarning;
      std::call_once(dualSourceWarning, []() {
        SoDebugError::postWarning(
          "SoGLRenderBackend::bindVisualCommand",
          "Dual-source blend factors are not supported by the Visual "
          "program; using primary-source factors for execution.");
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
  else {
    glDisable(GL_BLEND);
  }
}

void
SoGLRenderBackend::bindAlphaTest(const SoRenderCommand & command)
{
  const VisualProgram::Uniforms & uniforms = this->visualProgram.uniforms;
  this->glue->glUniform1i(
    uniforms.alphaTest.function,
    command.state.alphaTest.policy == SO_ALPHA_TEST_POLICY_NONE
      ? 0 : static_cast<GLint>(command.state.alphaTest.function));
  this->glue->glUniform1f(uniforms.alphaTest.reference,
                          command.state.alphaTest.reference);

}

void
SoGLRenderBackend::bindTexture(const SoRenderCommand & command,
                               const CachedCommand & entry)
{
  const VisualProgram::Uniforms & uniforms = this->visualProgram.uniforms;
  const bool textured = entry.texture != 0 && entry.texcoordBuffer != 0;
  this->glue->glUniform1f(uniforms.texture.enabled,
                          textured ? 1.0f : 0.0f);
  if (textured) {
    cc_glglue_glActiveTexture(this->glue, GL_TEXTURE0);
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, entry.texture);
    this->glue->glUniform1i(uniforms.texture.sampler, 0);
  }
  this->glue->glUniform1i(uniforms.texture.model,
                          static_cast<GLint>(command.material.texture.model));
  const SbVec4f & textureBlend = command.material.texture.blendColor;
  this->glue->glUniform4f(uniforms.texture.blendColor,
                          textureBlend[0], textureBlend[1],
                          textureBlend[2], textureBlend[3]);
}

void
SoGLRenderBackend::bindVisualCommand(const SoDrawList & drawlist,
                                     const SoRenderCommand & command,
                                     const CachedCommand & entry,
                                     const SbMat & viewMat,
                                     const SbMat & projMat,
                                     const SoRenderParams & params)
{
  applyViewport(params);
  this->bindTransforms(command, viewMat, projMat);
  this->bindMaterial(command, entry);
  this->uploadLighting(drawlist, command);
  this->applyDepthState(command);
  this->applyBlendState(command);
  this->bindAlphaTest(command);
  this->bindTexture(command, entry);
}

void
SoGLRenderBackend::drawGeometry(const SoRenderCommand & command,
                                const CachedCommand & entry)
{
  const GLenum primitive = topologyToGL(command.geometry.topology);
  this->glue->glBindVertexArray(entry.vertexArray);
  if (command.geometry.indexCount && command.geometry.indices) {
    cc_glglue_glDrawElements(this->glue, primitive,
                             static_cast<GLsizei>(command.geometry.indexCount),
                             GL_UNSIGNED_INT, nullptr);
  }
  else {
    cc_glglue_glDrawArrays(this->glue, primitive, 0,
                           static_cast<GLsizei>(command.geometry.vertexCount));
  }
  this->glue->glBindVertexArray(0);
  if (entry.texture && entry.texcoordBuffer) {
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, 0);
  }
}

void
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

bool
SoGLRenderBackend::createShaders()
{
  return this->createVisualProgram();
}

bool
SoGLRenderBackend::createVisualProgram()
{
  const GLuint program = linkProgram(
    this->glue,
    coin_gl_visual_vertex_shadersource,
    coin_gl_visual_fragment_shadersource);
  if (!program) return false;

  this->visualProgram.handle = program;
  VisualProgram::Uniforms & uniforms = this->visualProgram.uniforms;
  uniforms.transforms.view = cc_glglue_glGetUniformLocation(this->glue, program, "u_view");
  uniforms.transforms.projection = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_proj");
  uniforms.transforms.model = cc_glglue_glGetUniformLocation(this->glue, program, "u_model");
  uniforms.material.color = cc_glglue_glGetUniformLocation(this->glue, program, "u_color");
  uniforms.material.useVertexColor = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_useVertexColor");
  uniforms.material.shadingModel = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_shadingModel");
  uniforms.material.emissiveColor = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_emissiveColor");
  uniforms.material.ambient = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_materialAmbient");
  uniforms.material.specular = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_materialSpecular");
  uniforms.material.shininess = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_materialShininess");
  uniforms.material.twoSidedLighting = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_twoSidedLighting");
  uniforms.material.vertexColorAlphaIncludesOpacity = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_vertexColorAlphaIncludesOpacity");
  uniforms.texture.alphaIncludesOpacity = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_textureAlphaIncludesOpacity");
  uniforms.texture.hasAlpha = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_textureHasAlpha");
  uniforms.lighting.ambient = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_ambientLight");
  uniforms.lighting.lightCount = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_lightCount");
  uniforms.lighting.lightType = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_lightType");
  uniforms.lighting.lightColor = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_lightColor");
  uniforms.lighting.lightDirection = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_lightDirection");
  uniforms.lighting.lightPosition = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_lightPosition");
  uniforms.lighting.lightAttenuation = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_lightAttenuation");
  uniforms.lighting.lightSpotParams = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_lightSpotParams");
  uniforms.texture.sampler = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_texture");
  uniforms.texture.enabled = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_textureEnabled");
  uniforms.texture.model = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_textureModel");
  uniforms.texture.blendColor = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_textureBlendColor");
  uniforms.alphaTest.function = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_alphaTestFunction");
  uniforms.alphaTest.reference = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_alphaTestReference");
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
