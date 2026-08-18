// src/rendering/SoGLRenderBackend.cpp

#include "rendering/SoGLRenderBackend.h"

#include <Inventor/errors/SoDebugError.h>

#include "glue/glp.h"
#include "glue/glslp.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <string>
#include <vector>

#include <data/shaders/gl/visual/Fragment.h>
#include <data/shaders/gl/visual/Vertex.h>

namespace {

static constexpr int MAX_VERTEX_COUNT = 10000000;
static constexpr GLuint POSITION_ATTRIBUTE = 0;
static constexpr GLuint COLOR_ATTRIBUTE = 1;
static constexpr GLuint TEXCOORD_ATTRIBUTE = 2;

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
      !this->glue->glVertexAttrib2f ||
      !this->glue->glUniform1f || !this->glue->glUniform1i ||
      !this->glue->glUniform4f || !this->glue->glUniformMatrix4fv) {
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
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
  entry.colorsKey = geometry.colors;
  entry.texcoordsKey = geometry.texcoords;
  entry.texturePixelsKey = hasTexture ? texture.pixels : nullptr;
  entry.indicesKey = geometry.indices;
  entry.vertexCount = geometry.vertexCount;
  entry.indexCount = geometry.indexCount;
  entry.vertexStride = vertexStride;
  entry.texcoordStride = geometry.texcoordStride;
  entry.textureWidth = hasTexture ? texture.width : 0;
  entry.textureHeight = hasTexture ? texture.height : 0;
  entry.textureComponents = hasTexture ? texture.numComponents : 0;
  entry.geometryCacheKey = geometry.cacheKey;
  entry.geometryRevision = geometry.revision;
  entry.textureCacheKey = hasTexture ? texture.cacheKey : 0;
  entry.textureRevision = hasTexture ? texture.revision : 0;
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
  const bool identityMatches = texture.cacheKey != 0
    ? entry.textureCacheKey == texture.cacheKey &&
      entry.textureRevision == texture.revision
    : entry.texturePixelsKey == texture.pixels;
  return identityMatches &&
    entry.textureWidth == texture.width &&
    entry.textureHeight == texture.height &&
    entry.textureComponents == texture.numComponents;
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
      : entry.positionsKey == geometry.positions;
    const bool geometryMatches = entry.positionBuffer != 0 &&
      identityMatches &&
      entry.colorsKey == geometry.colors &&
      entry.texcoordsKey == geometry.texcoords &&
      entry.indicesKey == geometry.indices &&
      entry.vertexCount == geometry.vertexCount &&
      entry.indexCount == geometry.indexCount &&
      entry.vertexStride == vertexStride &&
      entry.texcoordStride == geometry.texcoordStride &&
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
SoGLRenderBackend::drawCommand(const SoRenderCommand & command,
                                const SbMat & viewMat,
                                const SbMat & projMat,
                                const SoRenderParams & params)
{
  if ((!command.geometry.positions && command.geometry.cacheKey == 0) ||
      command.geometry.vertexCount == 0) return;
  const auto found = this->commandToCache.find(&command);
  if (found == this->commandToCache.end()) return;
  const CachedCommand & entry = this->gpuCache[found->second];
  if (!entry.vertexArray) return;

  this->bindVisualCommand(command, entry, viewMat, projMat, params);
  this->drawGeometry(command, entry);
}

void
SoGLRenderBackend::bindVisualCommand(const SoRenderCommand & command,
                                     const CachedCommand & entry,
                                     const SbMat & viewMat,
                                     const SbMat & projMat,
                                     const SoRenderParams & params)
{
  applyViewport(params);
  const VisualProgram::Uniforms & uniforms = this->visualProgram.uniforms;
  this->glue->glUniformMatrix4fv(uniforms.view, 1, GL_FALSE,
                                 &viewMat[0][0]);
  this->glue->glUniformMatrix4fv(uniforms.projection, 1, GL_FALSE,
                                 &projMat[0][0]);
  SbMat model;
  command.modelMatrix.getValue(model);
  this->glue->glUniformMatrix4fv(uniforms.model, 1, GL_FALSE,
                                 &model[0][0]);

  const SbVec4f & color = command.material.diffuse;
  this->glue->glUniform4f(uniforms.color,
                          color[0], color[1], color[2], color[3]);
  this->glue->glUniform1f(uniforms.useVertexColor,
                          entry.colorBuffer ? 1.0f : 0.0f);

  const bool textured = entry.texture != 0 && entry.texcoordBuffer != 0;
  this->glue->glUniform1f(uniforms.textureEnabled,
                          textured ? 1.0f : 0.0f);
  if (textured) {
    cc_glglue_glActiveTexture(this->glue, GL_TEXTURE0);
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, entry.texture);
    this->glue->glUniform1i(uniforms.texture, 0);
    this->glue->glUniform4f(uniforms.textureModulation,
                            color[0], color[1], color[2], color[3]);
  }
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
  uniforms.view = cc_glglue_glGetUniformLocation(this->glue, program, "u_view");
  uniforms.projection = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_proj");
  uniforms.model = cc_glglue_glGetUniformLocation(this->glue, program, "u_model");
  uniforms.color = cc_glglue_glGetUniformLocation(this->glue, program, "u_color");
  uniforms.useVertexColor = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_useVertexColor");
  uniforms.texture = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_texture");
  uniforms.textureEnabled = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_textureEnabled");
  uniforms.textureModulation = cc_glglue_glGetUniformLocation(
    this->glue, program, "u_texModColor");
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
    this->drawCommand(drawlist.getCommand(static_cast<int>(commandIndex)),
                      view, projection, params);
  }
  cc_glglue_glUseProgram(this->glue, 0);
  return TRUE;
}
