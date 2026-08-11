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
#include <cstring>
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
#include <data/shaders/gl/picking/Fragment.h>
#include <data/shaders/gl/picking/WideLineFragment.h>
#include <data/shaders/gl/picking/PixelFragment.h>
#include <data/shaders/gl/selection/Fragment.h>
#include <data/shaders/gl/selection/WideLineFragment.h>
#include <data/shaders/gl/selection/PixelFragment.h>

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

class ScopedPixelPackState {
public:
  ScopedPixelPackState()
  {
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
    glGetIntegerv(GL_PACK_SWAP_BYTES, &swapBytes);
    glGetIntegerv(GL_PACK_LSB_FIRST, &lsbFirst);
    glGetIntegerv(GL_PACK_IMAGE_HEIGHT, &imageHeight);
    glGetIntegerv(GL_PACK_SKIP_IMAGES, &skipImages);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE);
    glPixelStorei(GL_PACK_LSB_FIRST, GL_FALSE);
    glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
    glPixelStorei(GL_PACK_SKIP_IMAGES, 0);
  }

  ~ScopedPixelPackState()
  {
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
    glPixelStorei(GL_PACK_SWAP_BYTES, swapBytes);
    glPixelStorei(GL_PACK_LSB_FIRST, lsbFirst);
    glPixelStorei(GL_PACK_IMAGE_HEIGHT, imageHeight);
    glPixelStorei(GL_PACK_SKIP_IMAGES, skipImages);
  }

private:
  GLint alignment = 4;
  GLint rowLength = 0;
  GLint skipRows = 0;
  GLint skipPixels = 0;
  GLint swapBytes = GL_FALSE;
  GLint lsbFirst = GL_FALSE;
  GLint imageHeight = 0;
  GLint skipImages = 0;
};

// Picking and selection are explicit operations on a caller-owned GL
// context. They must not leak the temporary state used to implement them.
class ScopedGLState {
public:
  explicit ScopedGLState(const cc_glglue * glue)
    : glue(glue)
  {
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
    glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
    glGetIntegerv(GL_READ_BUFFER, &readBuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementArrayBuffer);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pixelPackBuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
    glGetIntegerv(GL_CULL_FACE_MODE, &cullFace);
    glGetIntegerv(GL_FRONT_FACE, &frontFace);
    glGetIntegerv(GL_POLYGON_MODE, polygonMode);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSourceRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDestinationRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSourceAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDestinationAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    glGetDoublev(GL_DEPTH_RANGE, depthRange);
    glGetFloatv(GL_LINE_WIDTH, &lineWidth);
    glGetFloatv(GL_POINT_SIZE, &pointSize);
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &polygonOffsetFactor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &polygonOffsetUnits);

    activeTexture = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    cc_glglue_glActiveTexture(this->glue, GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0);
    cc_glglue_glActiveTexture(this->glue, GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture1);
    cc_glglue_glActiveTexture(this->glue,
                              static_cast<GLenum>(activeTexture));

    blend = glIsEnabled(GL_BLEND);
    scissor = glIsEnabled(GL_SCISSOR_TEST);
    depth = glIsEnabled(GL_DEPTH_TEST);
    cull = glIsEnabled(GL_CULL_FACE);
    offsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    offsetLine = glIsEnabled(GL_POLYGON_OFFSET_LINE);
    offsetPoint = glIsEnabled(GL_POLYGON_OFFSET_POINT);
  }

  ~ScopedGLState()
  {
    cc_glglue_glBindFramebuffer(this->glue, GL_DRAW_FRAMEBUFFER,
                                static_cast<GLuint>(drawFramebuffer));
    cc_glglue_glBindFramebuffer(this->glue, GL_READ_FRAMEBUFFER,
                                static_cast<GLuint>(readFramebuffer));
    cc_glglue_glBindRenderbuffer(this->glue, GL_RENDERBUFFER,
                                 static_cast<GLuint>(renderbuffer));
    glDrawBuffer(static_cast<GLenum>(drawBuffer));
    glReadBuffer(static_cast<GLenum>(readBuffer));

    cc_glglue_glActiveTexture(this->glue, GL_TEXTURE0);
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D,
                            static_cast<GLuint>(texture0));
    cc_glglue_glActiveTexture(this->glue, GL_TEXTURE1);
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D,
                            static_cast<GLuint>(texture1));
    cc_glglue_glActiveTexture(this->glue,
                              static_cast<GLenum>(activeTexture));

    if (this->glue->glBindVertexArray) {
      this->glue->glBindVertexArray(static_cast<GLuint>(vertexArray));
    }
    cc_glglue_glBindBuffer(this->glue, GL_ARRAY_BUFFER,
                           static_cast<GLuint>(arrayBuffer));
    cc_glglue_glBindBuffer(this->glue, GL_ELEMENT_ARRAY_BUFFER,
                           static_cast<GLuint>(elementArrayBuffer));
    cc_glglue_glBindBuffer(this->glue, GL_PIXEL_PACK_BUFFER,
                           static_cast<GLuint>(pixelPackBuffer));
    cc_glglue_glUseProgram(this->glue, static_cast<GLuint>(program));

    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
    glDepthFunc(static_cast<GLenum>(depthFunction));
    glCullFace(static_cast<GLenum>(cullFace));
    glFrontFace(static_cast<GLenum>(frontFace));
    if (polygonMode[0] == polygonMode[1]) {
      glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygonMode[0]));
    }
    else {
      glPolygonMode(GL_FRONT, static_cast<GLenum>(polygonMode[0]));
      glPolygonMode(GL_BACK, static_cast<GLenum>(polygonMode[1]));
    }
    cc_glglue_glBlendFuncSeparate(
      this->glue, static_cast<GLenum>(blendSourceRGB),
      static_cast<GLenum>(blendDestinationRGB),
      static_cast<GLenum>(blendSourceAlpha),
      static_cast<GLenum>(blendDestinationAlpha));
    if (this->glue->glBlendEquationSeparate) {
      this->glue->glBlendEquationSeparate(
        static_cast<GLenum>(blendEquationRGB),
        static_cast<GLenum>(blendEquationAlpha));
    }
    else if (cc_glglue_has_blendequation(this->glue) &&
             blendEquationRGB == blendEquationAlpha) {
      cc_glglue_glBlendEquation(this->glue,
                                static_cast<GLenum>(blendEquationRGB));
    }
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glDepthMask(depthWrite);
    glDepthRange(depthRange[0], depthRange[1]);
    glLineWidth(lineWidth);
    glPointSize(pointSize);
    glPolygonOffset(polygonOffsetFactor, polygonOffsetUnits);

    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (offsetFill) glEnable(GL_POLYGON_OFFSET_FILL);
    else glDisable(GL_POLYGON_OFFSET_FILL);
    if (offsetLine) glEnable(GL_POLYGON_OFFSET_LINE);
    else glDisable(GL_POLYGON_OFFSET_LINE);
    if (offsetPoint) glEnable(GL_POLYGON_OFFSET_POINT);
    else glDisable(GL_POLYGON_OFFSET_POINT);
  }

private:
  const cc_glglue * glue;
  GLint drawFramebuffer = 0;
  GLint readFramebuffer = 0;
  GLint renderbuffer = 0;
  GLint drawBuffer = GL_BACK;
  GLint readBuffer = GL_BACK;
  GLint program = 0;
  GLint vertexArray = 0;
  GLint arrayBuffer = 0;
  GLint elementArrayBuffer = 0;
  GLint pixelPackBuffer = 0;
  GLint viewport[4] = { 0, 0, 0, 0 };
  GLint scissorBox[4] = { 0, 0, 0, 0 };
  GLint depthFunction = GL_LESS;
  GLint cullFace = GL_BACK;
  GLint frontFace = GL_CCW;
  GLint polygonMode[2] = { GL_FILL, GL_FILL };
  GLint blendSourceRGB = GL_ONE;
  GLint blendDestinationRGB = GL_ZERO;
  GLint blendSourceAlpha = GL_ONE;
  GLint blendDestinationAlpha = GL_ZERO;
  GLint blendEquationRGB = GL_FUNC_ADD;
  GLint blendEquationAlpha = GL_FUNC_ADD;
  GLboolean colorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
  GLboolean depthWrite = GL_TRUE;
  GLdouble depthRange[2] = { 0.0, 1.0 };
  GLfloat lineWidth = 1.0f;
  GLfloat pointSize = 1.0f;
  GLfloat polygonOffsetFactor = 0.0f;
  GLfloat polygonOffsetUnits = 0.0f;
  GLint activeTexture = GL_TEXTURE0;
  GLint texture0 = 0;
  GLint texture1 = 0;
  GLboolean blend = GL_FALSE;
  GLboolean scissor = GL_FALSE;
  GLboolean depth = GL_FALSE;
  GLboolean cull = GL_FALSE;
  GLboolean offsetFill = GL_FALSE;
  GLboolean offsetLine = GL_FALSE;
  GLboolean offsetPoint = GL_FALSE;
};
} // namespace

SoGLRenderBackend::SoGLRenderBackend()
{
}

SoGLRenderBackend::~SoGLRenderBackend()
{
  if (!this->isInitialized()) return;
  if (coin_gl_current_context() == this->context) this->shutdown();
  else this->discard();
}

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
  this->context = coin_gl_current_context();
  this->glue = this->context
    ? cc_glglue_instance_from_context_ptr(this->context) : nullptr;
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
    this->context = nullptr;
    return FALSE;
  }

  if (!this->createShaders()) {
    this->emitError("failed to create retained OpenGL 3.3 shaders");
    this->glue = nullptr;
    this->context = nullptr;
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
  if (coin_gl_current_context() != this->context) {
    this->discard();
    return;
  }
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
  if (this->pickPrograms.visual.handle) {
    cc_glglue_glDeleteProgram(this->glue, this->pickPrograms.visual.handle);
    this->pickPrograms.visual.handle = 0;
  }
  if (this->pickPrograms.line.handle) {
    cc_glglue_glDeleteProgram(this->glue, this->pickPrograms.line.handle);
    this->pickPrograms.line.handle = 0;
  }
  if (this->pickPrograms.triangleLine.handle) {
    cc_glglue_glDeleteProgram(this->glue,
                              this->pickPrograms.triangleLine.handle);
    this->pickPrograms.triangleLine.handle = 0;
  }
  if (this->pickPrograms.point.handle) {
    cc_glglue_glDeleteProgram(this->glue, this->pickPrograms.point.handle);
    this->pickPrograms.point.handle = 0;
  }
  if (this->pickPrograms.trianglePoint.handle) {
    cc_glglue_glDeleteProgram(this->glue,
                              this->pickPrograms.trianglePoint.handle);
    this->pickPrograms.trianglePoint.handle = 0;
  }
  if (this->pickPrograms.pixel.handle) {
    cc_glglue_glDeleteProgram(this->glue, this->pickPrograms.pixel.handle);
    this->pickPrograms.pixel.handle = 0;
  }
  auto deleteProgram = [this](GLuint & handle) {
    if (!handle) return;
    cc_glglue_glDeleteProgram(this->glue, handle);
    handle = 0;
  };
  deleteProgram(this->selectionPrograms.visual.handle);
  deleteProgram(this->selectionPrograms.line.handle);
  deleteProgram(this->selectionPrograms.triangleLine.handle);
  deleteProgram(this->selectionPrograms.point.handle);
  deleteProgram(this->selectionPrograms.trianglePoint.handle);
  deleteProgram(this->selectionPrograms.pixel.handle);
  this->destroyPickFramebuffer();
  this->pickTarget.lookup.clear();
  this->pickTarget.generation = 0;
  this->pickTarget.ready = false;
  this->glue = nullptr;
  this->context = nullptr;
  this->setInitialized(FALSE);
  this->emitLog("shutdown");
}

void
SoGLRenderBackend::discard()
{
  this->gpuCache.clear();
  this->commandToCache.clear();
  this->cachedCommandCount = 0;
  this->haveCacheGeneration = false;
  this->cacheGeneration = 0;
  this->visualProgram = VisualProgram();
  this->rasterPrograms = RasterPrograms();
  this->pickPrograms = PickPrograms();
  this->selectionPrograms = PickPrograms();
  this->pickTarget = PickTarget();
  this->glue = nullptr;
  this->context = nullptr;
  this->setInitialized(FALSE);
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

SoGLRenderBackend::CommandFrame
SoGLRenderBackend::effectiveCommandFrame(const SoRenderCommand & command,
                                         const SoRenderParams & params,
                                         const bool framebufferLocal) const
{
  CommandFrame frame;
  params.viewMatrix.getValue(frame.view);
  params.projMatrix.getValue(frame.projection);
  if (command.state.useCommandMatrices) {
    command.viewMatrix.getValue(frame.view);
    command.projMatrix.getValue(frame.projection);
  }

  const SbVec2s baseOrigin = params.viewport.getViewportOriginPixels();
  if (command.state.raster.viewportOverride) {
    frame.viewportOrigin = SbVec2s(
      static_cast<short>(command.state.raster.viewportX),
      static_cast<short>(command.state.raster.viewportY));
    frame.viewportSize = SbVec2s(
      static_cast<short>(command.state.raster.viewportWidth),
      static_cast<short>(command.state.raster.viewportHeight));
  }
  else {
    frame.viewportOrigin = baseOrigin;
    frame.viewportSize = params.viewport.getViewportSizePixels();
  }

  if (framebufferLocal) {
    frame.viewportOrigin -= baseOrigin;
  }
  return frame;
}

void
SoGLRenderBackend::clearDepthEvent(const SoDepthClearEvent & event,
                                   const SoRenderParams & params,
                                   const bool framebufferLocal)
{
  GLint oldScissor[4] = {0, 0, 0, 0};
  glGetIntegerv(GL_SCISSOR_BOX, oldScissor);
  const GLboolean oldScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
  GLboolean oldDepthMask = GL_TRUE;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &oldDepthMask);
  GLfloat oldClearDepth = 1.0f;
  glGetFloatv(GL_DEPTH_CLEAR_VALUE, &oldClearDepth);

  const SbVec2s baseOrigin = params.viewport.getViewportOriginPixels();
  int x = baseOrigin[0];
  int y = baseOrigin[1];
  int width = params.viewport.getViewportSizePixels()[0];
  int height = params.viewport.getViewportSizePixels()[1];
  if (event.viewportOverride) {
    x = event.viewportX;
    y = event.viewportY;
    width = event.viewportWidth;
    height = event.viewportHeight;
  }
  if (framebufferLocal) {
    x -= baseOrigin[0];
    y -= baseOrigin[1];
  }

  if (width > 0 && height > 0) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, width, height);
    glDepthMask(GL_TRUE);
    glClearDepth(params.clearDepth);
    glClear(GL_DEPTH_BUFFER_BIT);
  }

  glScissor(oldScissor[0], oldScissor[1], oldScissor[2], oldScissor[3]);
  if (oldScissorEnabled) glEnable(GL_SCISSOR_TEST);
  else glDisable(GL_SCISSOR_TEST);
  glDepthMask(oldDepthMask);
  glClearDepth(oldClearDepth);
}

void
SoGLRenderBackend::drawCommand(const SoDrawList & drawlist,
                               const SoRenderCommand & command,
                               const SbMat & viewMat,
                               const SbMat & projMat,
                               const SoRenderParams & params)
{
  (void) viewMat;
  (void) projMat;
  if (!command.state.raster.visible) return;
  if ((!command.geometry.positions && command.geometry.cacheKey == 0) ||
      command.geometry.vertexCount == 0) return;
  if (command.state.raster.viewportOverride &&
      !command.state.raster.viewportEnabled) return;
  const auto found = this->commandToCache.find(&command);
  if (found == this->commandToCache.end()) return;
  CachedCommand & entry = this->gpuCache[found->second];
  if (!entry.vertexArray) return;

  const CommandFrame frame = this->effectiveCommandFrame(command, params, false);
  if (frame.viewportSize[0] <= 0 || frame.viewportSize[1] <= 0) return;
  RasterPath path = this->selectRasterPath(entry, command, params);
  const SbVec2s & viewportSize = frame.viewportSize;
  glViewport(frame.viewportOrigin[0], frame.viewportOrigin[1],
             viewportSize[0], viewportSize[1]);
  if (path.useLineShader &&
      (path.primitive == GL_LINES || path.primitive == GL_LINE_STRIP)) {
    this->updateLineDistances(entry, command, frame.view, frame.projection,
                              viewportSize);
    path.expandedLineStream = command.geometry.indices &&
      command.geometry.indexCount && entry.lineRasterVertexArray != 0;
  }

  const SbVec4f & color = command.material.diffuse;
  this->applyDepthState(command);
  this->applyRasterState(command, path);
  this->applyBlendState(command, color);
  GLenum polygonOffsetTarget = GL_POLYGON_OFFSET_FILL;
  const bool polygonOffset = this->applyPolygonOffset(
    command, path, polygonOffsetTarget);
  this->bindCommandProgram(drawlist, command, path, frame.view, frame.projection,
                           frame.viewportOrigin, viewportSize, entry);
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
  // Core-profile drivers may report a broad GL_LINE_WIDTH_RANGE while still
  // rasterizing ordinary wide lines as one pixel. Use the retained line
  // shader for every non-default width so compat and core profiles execute
  // the same semantic raster state.
  const bool lineEmulationRequired =
    path.lineWidth > 1.0f ||
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
                                      const SbVec2s & viewportOrigin,
                                      const SbVec2s & viewportSize,
                                      const CachedCommand & entry)
{
  if (path.textured) {
    cc_glglue_glActiveTexture(this->glue, GL_TEXTURE0);
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, entry.texture);
  }
  const SbVec4f & color = command.material.diffuse;
  if (path.pixelRaster) {
    this->bindPixelShader(command, viewMat, projMat,
                          viewportOrigin,
                          viewportSize);
  }
  else if (path.usePointShader) {
    this->bindPointShader(command, viewMat, projMat, color,
                          entry.colorBuffer != 0, path.pointSize,
                          viewportSize,
                          path.pointTriangleInput, drawlist, path.textured);
  }
  else if (path.useLineShader) {
    this->bindLineShader(command, viewMat, projMat, color,
                         entry.colorBuffer != 0, path.lineWidth,
                         viewportSize,
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
SoGLRenderBackend::renderStageRange(const SoDrawList & drawlist,
                                    const SoRenderStage stage,
                                    const uint32_t begin,
                                    const uint32_t end,
                                    const SoRenderParams & params)
{
  std::vector<int> order;
  const uint32_t limit = std::min(end,
                                 static_cast<uint32_t>(drawlist.getNumCommands()));
  const uint32_t start = std::min(begin, limit);
  order.reserve(limit - start);
  for (uint32_t i = start; i < limit; ++i) {
    order.push_back(static_cast<int>(i));
  }
  std::stable_sort(order.begin(), order.end(), [&drawlist](const int lhs,
                                                            const int rhs) {
    return drawlist.getCommand(lhs).sortKey <
           drawlist.getCommand(rhs).sortKey;
  });
  SbMat viewMat;
  SbMat projMat;
  params.viewMatrix.getValue(viewMat);
  params.projMatrix.getValue(projMat);
  for (const int index : order) {
    const SoRenderCommand & command = drawlist.getCommand(index);
    if (command.stage == stage) {
      this->drawCommand(drawlist, command, viewMat, projMat, params);
    }
  }
}

void
SoGLRenderBackend::renderStage(const SoDrawList & drawlist,
                               const SoRenderStage stage,
                               const SoRenderParams & params,
                               const bool framebufferLocal)
{
  uint32_t begin = 0;
  const uint32_t commandCount = static_cast<uint32_t>(drawlist.getNumCommands());
  for (const SoDepthClearEvent & event : drawlist.getDepthClearEvents()) {
    if (event.stage != stage) continue;
    const uint32_t barrier = std::min(event.sequence, commandCount);
    this->renderStageRange(drawlist, stage, begin, barrier, params);
    this->clearDepthEvent(event, params, framebufferLocal);
    begin = barrier;
  }
  this->renderStageRange(drawlist, stage, begin, commandCount, params);
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
    GLfloat oldClearDepth = 1.0f;
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, &oldClearDepth);
    glClearDepth(params.clearDepth);
    clearMask |= GL_DEPTH_BUFFER_BIT;
    if (clearMask) {
      glClear(clearMask);
      glClearDepth(oldClearDepth);
      clearMask = 0;
    }
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
  this->pickPrograms.visual.handle = linkProgram(
    this->glue, coin_gl_visual_vertex_shadersource,
    coin_gl_picking_fragment_shadersource);
  this->pickPrograms.line.handle = linkProgram(
    this->glue, coin_gl_wide_line_vertex_shadersource,
    coin_gl_picking_wide_line_fragment_shadersource,
    coin_gl_wide_line_geometry_shadersource);
  this->pickPrograms.triangleLine.handle = linkProgram(
    this->glue, coin_gl_wide_line_vertex_shadersource,
    coin_gl_picking_wide_line_fragment_shadersource,
    coin_gl_wide_line_triangle_geometry_shadersource);
  this->pickPrograms.point.handle = linkProgram(
    this->glue, coin_gl_point_vertex_shadersource,
    coin_gl_picking_fragment_shadersource,
    coin_gl_point_geometry_shadersource);
  this->pickPrograms.trianglePoint.handle = linkProgram(
    this->glue, coin_gl_point_vertex_shadersource,
    coin_gl_picking_fragment_shadersource,
    coin_gl_point_triangle_geometry_shadersource);
  this->pickPrograms.pixel.handle = linkProgram(
    this->glue, coin_gl_pixel_vertex_shadersource,
    coin_gl_picking_pixel_fragment_shadersource);
  this->selectionPrograms.visual.handle = linkProgram(
    this->glue, coin_gl_visual_vertex_shadersource,
    coin_gl_selection_fragment_shadersource);
  this->selectionPrograms.line.handle = linkProgram(
    this->glue, coin_gl_wide_line_vertex_shadersource,
    coin_gl_selection_wide_line_fragment_shadersource,
    coin_gl_wide_line_geometry_shadersource);
  this->selectionPrograms.triangleLine.handle = linkProgram(
    this->glue, coin_gl_wide_line_vertex_shadersource,
    coin_gl_selection_wide_line_fragment_shadersource,
    coin_gl_wide_line_triangle_geometry_shadersource);
  this->selectionPrograms.point.handle = linkProgram(
    this->glue, coin_gl_point_vertex_shadersource,
    coin_gl_selection_fragment_shadersource,
    coin_gl_point_geometry_shadersource);
  this->selectionPrograms.trianglePoint.handle = linkProgram(
    this->glue, coin_gl_point_vertex_shadersource,
    coin_gl_selection_fragment_shadersource,
    coin_gl_point_triangle_geometry_shadersource);
  this->selectionPrograms.pixel.handle = linkProgram(
    this->glue, coin_gl_pixel_vertex_shadersource,
    coin_gl_selection_pixel_fragment_shadersource);

  const GLuint programs[] = {
    this->visualProgram.handle,
    this->rasterPrograms.line.handle,
    this->rasterPrograms.triangleLine.handle,
    this->rasterPrograms.point.handle,
    this->rasterPrograms.trianglePoint.handle,
    this->rasterPrograms.pixel.handle,
    this->pickPrograms.visual.handle,
    this->pickPrograms.line.handle,
    this->pickPrograms.triangleLine.handle,
    this->pickPrograms.point.handle,
    this->pickPrograms.trianglePoint.handle,
    this->pickPrograms.pixel.handle,
    this->selectionPrograms.visual.handle,
    this->selectionPrograms.line.handle,
    this->selectionPrograms.triangleLine.handle,
    this->selectionPrograms.point.handle,
    this->selectionPrograms.trianglePoint.handle,
    this->selectionPrograms.pixel.handle
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
      this->pickPrograms.visual.handle = 0;
      this->pickPrograms.line.handle = 0;
      this->pickPrograms.triangleLine.handle = 0;
      this->pickPrograms.point.handle = 0;
      this->pickPrograms.trianglePoint.handle = 0;
      this->pickPrograms.pixel.handle = 0;
      this->selectionPrograms.visual.handle = 0;
      this->selectionPrograms.line.handle = 0;
      this->selectionPrograms.triangleLine.handle = 0;
      this->selectionPrograms.point.handle = 0;
      this->selectionPrograms.trianglePoint.handle = 0;
      this->selectionPrograms.pixel.handle = 0;
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
  auto cachePickUniforms = [this, &uniform](PickProgram & program) {
    PickProgram::Uniforms & u = program.uniforms;
    u.view = uniform(program.handle, "u_view");
    u.proj = uniform(program.handle, "u_proj");
    u.model = uniform(program.handle, "u_model");
    u.color = uniform(program.handle, "u_color");
    u.useVertexColor = uniform(program.handle, "u_useVertexColor");
    u.vertexColorAlphaIncludesOpacity = uniform(
      program.handle, "u_vertexColorAlphaIncludesOpacity");
    u.textureAlphaIncludesOpacity = uniform(
      program.handle, "u_textureAlphaIncludesOpacity");
    u.textureHasAlpha = uniform(program.handle, "u_textureHasAlpha");
    u.textureEnabled = uniform(program.handle, "u_textureEnabled");
    u.textureModel = uniform(program.handle, "u_textureModel");
    u.textureBlendColor = uniform(program.handle, "u_textureBlendColor");
    u.texture = uniform(program.handle, "u_texture");
    u.selectionColor = uniform(program.handle, "u_selectionColor");
    u.alphaTestFunction = uniform(program.handle, "u_alphaTestFunction");
    u.alphaTestReference = uniform(program.handle, "u_alphaTestReference");
    u.pickId = uniform(program.handle, "u_pickId");
    u.vpSize = uniform(program.handle, "u_vpSize");
    u.lineWidth = uniform(program.handle, "u_lineWidth");
    u.pointSize = uniform(program.handle, "u_pointSize");
    u.stipplePattern = uniform(program.handle, "u_stipplePattern");
    u.stippleScale = uniform(program.handle, "u_stippleScale");
    u.cullBackFaces = uniform(program.handle, "u_cullBackFaces");
    u.frontFaceCCW = uniform(program.handle, "u_frontFaceCCW");
    u.quadCenter = uniform(program.handle, "u_quadCenter");
    u.sourceSize = uniform(program.handle, "u_sourceSize");
    u.rasterSize = uniform(program.handle, "u_rasterSize");
    u.viewportOrigin = uniform(program.handle, "u_viewportOrigin");
    u.pixelOrigin = uniform(program.handle, "u_pixelOrigin");
    u.texModColor = uniform(program.handle, "u_texModColor");
    u.previousDepth = uniform(program.handle, "u_previousDepth");
    u.peelEnabled = uniform(program.handle, "u_peelEnabled");
  };
  cachePickUniforms(this->pickPrograms.visual);
  cachePickUniforms(this->pickPrograms.line);
  cachePickUniforms(this->pickPrograms.triangleLine);
  cachePickUniforms(this->pickPrograms.point);
  cachePickUniforms(this->pickPrograms.trianglePoint);
  cachePickUniforms(this->pickPrograms.pixel);
  cachePickUniforms(this->selectionPrograms.visual);
  cachePickUniforms(this->selectionPrograms.line);
  cachePickUniforms(this->selectionPrograms.triangleLine);
  cachePickUniforms(this->selectionPrograms.point);
  cachePickUniforms(this->selectionPrograms.trianglePoint);
  cachePickUniforms(this->selectionPrograms.pixel);
  return true;
}

void
SoGLRenderBackend::destroyPickFramebuffer()
{
  if (!this->glue || !this->glue->has_fbo) {
    this->pickTarget.framebuffer = 0;
    this->pickTarget.colorTexture = 0;
    this->pickTarget.depthTextures[0] = 0;
    this->pickTarget.depthTextures[1] = 0;
    this->pickTarget.size = SbVec2s(0, 0);
    return;
  }
  if (this->pickTarget.colorTexture) {
    cc_glglue_glDeleteTextures(this->glue, 1, &this->pickTarget.colorTexture);
  }
  if (this->pickTarget.depthTextures[0] ||
      this->pickTarget.depthTextures[1]) {
    cc_glglue_glDeleteTextures(this->glue, 2,
                               this->pickTarget.depthTextures);
  }
  if (this->pickTarget.framebuffer) {
    cc_glglue_glDeleteFramebuffers(this->glue, 1, &this->pickTarget.framebuffer);
  }
  this->pickTarget.framebuffer = 0;
  this->pickTarget.colorTexture = 0;
  this->pickTarget.depthTextures[0] = 0;
  this->pickTarget.depthTextures[1] = 0;
  this->pickTarget.activeDepth = 0;
  this->pickTarget.size = SbVec2s(0, 0);
}

bool
SoGLRenderBackend::ensurePickFramebuffer(const SbVec2s & size)
{
  if (!this->glue || !this->glue->has_fbo ||
      !this->glue->glGenFramebuffers ||
      !this->glue->glBindFramebuffer ||
      !this->glue->glDeleteFramebuffers ||
      !this->glue->glCheckFramebufferStatus ||
      !this->glue->glFramebufferTexture2D ||
      !this->glue->glClearBufferuiv ||
      !this->glue->glClearBufferfv ||
      !this->glue->glUniform1ui) {
    SoDebugError::postWarning(
      "SoGLRenderBackend::ensurePickFramebuffer",
      "integer picking is not supported by the active GL context");
    return false;
  }
  if (size[0] <= 0 || size[1] <= 0) return false;
  if (this->pickTarget.framebuffer && this->pickTarget.size == size) return true;

  ScopedGLState state(this->glue);
  this->destroyPickFramebuffer();

  cc_glglue_glGenFramebuffers(this->glue, 1, &this->pickTarget.framebuffer);
  cc_glglue_glBindFramebuffer(this->glue, GL_FRAMEBUFFER,
                              this->pickTarget.framebuffer);

  cc_glglue_glGenTextures(this->glue, 1, &this->pickTarget.colorTexture);
  cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, this->pickTarget.colorTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, size[0], size[1], 0,
               GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
  cc_glglue_glFramebufferTexture2D(this->glue, GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   this->pickTarget.colorTexture, 0);

  cc_glglue_glGenTextures(this->glue, 2, this->pickTarget.depthTextures);
  for (int i = 0; i < 2; ++i) {
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D,
                            this->pickTarget.depthTextures[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
                 size[0], size[1], 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  }
  cc_glglue_glFramebufferTexture2D(this->glue, GL_FRAMEBUFFER,
                                   GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                   this->pickTarget.depthTextures[0], 0);
  this->pickTarget.activeDepth = 0;
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);

  const GLenum status = cc_glglue_glCheckFramebufferStatus(
    this->glue, GL_FRAMEBUFFER);
  const bool complete = status == GL_FRAMEBUFFER_COMPLETE;
  if (complete) {
    this->pickTarget.size = size;
  }
  else {
    SoDebugError::postWarning(
      "SoGLRenderBackend::ensurePickFramebuffer",
      "integer picking framebuffer is incomplete (status 0x%x)",
      static_cast<unsigned int>(status));
    this->destroyPickFramebuffer();
  }

  return complete;
}

void
SoGLRenderBackend::drawPickEntry(const SoDrawList & drawlist,
                                 const SoPickLUTEntry & entry,
                                 const GLuint id,
                                 const SbMat & viewMat,
                                 const SbMat & projMat,
                                 const SoRenderParams & params)
{
  this->drawCoverageEntry(drawlist, entry, id, SbColor4f(), viewMat,
                          projMat, params, false);
}

void
SoGLRenderBackend::drawSelectionEntry(const SoDrawList & drawlist,
                                      const SoPickLUTEntry & entry,
                                      const SbColor4f & color,
                                      const SbMat & viewMat,
                                      const SbMat & projMat,
                                      const SoRenderParams & params)
{
  this->drawCoverageEntry(drawlist, entry, 0, color, viewMat, projMat,
                          params, true);
}

void
SoGLRenderBackend::drawCoverageEntry(const SoDrawList & drawlist,
                                     const SoPickLUTEntry & entry,
                                     const GLuint id,
                                     const SbColor4f & selectionColor,
                                     const SbMat & viewMat,
                                     const SbMat & projMat,
                                     const SoRenderParams & params,
                                     const bool selection)
{
  (void) viewMat;
  (void) projMat;
  if (entry.commandIndex < 0 ||
      entry.commandIndex >= drawlist.getNumCommands()) return;
  const SoRenderCommand & command = drawlist.getCommand(entry.commandIndex);
  if (!command.state.raster.visible) return;
  if (!command.geometry.positions || command.geometry.vertexCount == 0) return;
  if (command.state.raster.viewportOverride &&
      !command.state.raster.viewportEnabled) return;

  const CommandFrame frame = this->effectiveCommandFrame(
    command, params, !selection);
  if (frame.viewportSize[0] <= 0 || frame.viewportSize[1] <= 0) return;
  glViewport(frame.viewportOrigin[0], frame.viewportOrigin[1],
             frame.viewportSize[0], frame.viewportSize[1]);

  const auto cacheIt = this->commandToCache.find(&command);
  if (cacheIt == this->commandToCache.end()) return;
  const CachedCommand & cache = this->gpuCache[cacheIt->second];
  if (!cache.vertexArray) return;

  const GLenum primitive = topologyToGL(command.geometry.topology);
  const bool textured = cache.texture != 0 && cache.texcoordBuffer != 0;
  const bool pixelRaster = textured && command.pixelRaster.enabled &&
    command.pixelRaster.width > 0 && command.pixelRaster.height > 0;
  const float dpr = params.devicePixelRatio > 0.0f
    ? params.devicePixelRatio : 1.0f;
  const float pointSize = std::max(1.0f, command.state.raster.pointSize) * dpr;
  const float lineWidth = std::max(1.0f, command.state.raster.lineWidth) * dpr;
  const SoRasterFillMode fillMode = command.state.raster.fillMode;
  const bool triangleTopology = primitive == GL_TRIANGLES ||
    primitive == GL_TRIANGLE_STRIP;
  const bool lineTopology = primitive == GL_LINES ||
    primitive == GL_LINE_STRIP;
  const bool pointTopology = primitive == GL_POINTS;
  const bool lineRaster = lineTopology ||
    (fillMode == SO_RASTER_LINES && triangleTopology);
  const bool pointRaster = pointTopology ||
    (fillMode == SO_RASTER_POINTS && triangleTopology);
  const bool lineEmulationRequired = lineWidth > this->rasterPrograms.nativeLineWidthMax ||
    command.state.raster.linePattern != 0xFFFF;
  const PickPrograms * programSet = selection
    ? &this->selectionPrograms : &this->pickPrograms;
  const bool usePointShader = !pixelRaster && pointRaster &&
    programSet->point.handle != 0 &&
    pointSize > this->rasterPrograms.nativePointSizeMax;
  const bool useLineShader = !pixelRaster && lineRaster &&
    programSet->line.handle != 0 &&
    lineEmulationRequired;
  const bool lineTriangleInput = useLineShader &&
    fillMode == SO_RASTER_LINES &&
    triangleTopology;
  const bool pointTriangleInput = usePointShader && fillMode == SO_RASTER_POINTS &&
    triangleTopology;

  if (useLineShader && lineTopology) {
    CachedCommand & mutableCache = this->gpuCache[cacheIt->second];
    this->updateLineDistances(mutableCache, command, frame.view,
                              frame.projection, frame.viewportSize);
  }
  const bool expandedLineStream = useLineShader && lineTopology &&
    command.geometry.indices && command.geometry.indexCount &&
    cache.lineRasterVertexArray != 0;

  GLuint program = programSet->visual.handle;
  const PickProgram::Uniforms * locations = &programSet->visual.uniforms;
  if (pixelRaster) {
    program = programSet->pixel.handle;
    locations = &programSet->pixel.uniforms;
  }
  else if (useLineShader) {
    program = lineTriangleInput ? programSet->triangleLine.handle
                                : programSet->line.handle;
    locations = lineTriangleInput
      ? &programSet->triangleLine.uniforms
      : &programSet->line.uniforms;
  }
  else if (usePointShader) {
    program = pointTriangleInput ? programSet->trianglePoint.handle
                                 : programSet->point.handle;
    locations = pointTriangleInput
      ? &programSet->trianglePoint.uniforms
      : &programSet->point.uniforms;
  }
  cc_glglue_glUseProgram(this->glue, program);

  auto uniformMatrix = [this](GLint location, const SbMat & matrix) {
    if (location >= 0) {
      this->glue->glUniformMatrix4fv(location, 1, GL_FALSE, &matrix[0][0]);
    }
  };
  auto uniform1f = [this](GLint location, float value) {
    if (location >= 0) this->glue->glUniform1f(location, value);
  };
  auto uniform1i = [this](GLint location, GLint value) {
    if (location >= 0) this->glue->glUniform1i(location, value);
  };
  auto uniform2f = [this](GLint location, float x, float y) {
    if (location >= 0) this->glue->glUniform2f(location, x, y);
  };
  auto uniform4f = [this](GLint location, const SbVec4f & value) {
    if (location >= 0) this->glue->glUniform4f(location, value[0], value[1],
                                                value[2], value[3]);
  };

  SbMat model;
  command.modelMatrix.getValue(model);
  uniformMatrix(locations->view, frame.view);
  uniformMatrix(locations->proj, frame.projection);
  uniformMatrix(locations->model, model);
  uniform4f(locations->color, command.material.diffuse);
  uniform1f(locations->useVertexColor, cache.colorBuffer ? 1.0f : 0.0f);
  uniform1f(locations->vertexColorAlphaIncludesOpacity,
            command.material.vertexColorAlphaIncludesOpacity ? 1.0f : 0.0f);
  uniform1f(locations->textureAlphaIncludesOpacity,
            command.material.textureAlphaIncludesOpacity ? 1.0f : 0.0f);
  const bool textureHasAlpha = command.material.texture.numComponents == 2 ||
    command.material.texture.numComponents == 4;
  uniform1f(locations->textureHasAlpha, textureHasAlpha ? 1.0f : 0.0f);
  uniform1f(locations->textureEnabled, textured ? 1.0f : 0.0f);
  uniform1i(locations->texture, 0);
  uniform1i(locations->textureModel,
            static_cast<GLint>(command.material.texture.model));
  uniform4f(locations->textureBlendColor,
            command.material.texture.blendColor);
  uniform1i(locations->alphaTestFunction,
            command.state.alphaTest.policy == SO_ALPHA_TEST_POLICY_NONE
              ? 0 : static_cast<GLint>(command.state.alphaTest.function));
  uniform1f(locations->alphaTestReference,
            command.state.alphaTest.reference);
  if (selection) {
    uniform4f(locations->selectionColor, selectionColor);
  }
  else {
    if (locations->pickId >= 0 && this->glue->glUniform1ui) {
      this->glue->glUniform1ui(locations->pickId, id);
    }
    uniform1i(locations->previousDepth, 1);
    uniform1i(locations->peelEnabled,
              this->pickTarget.peelEnabled ? 1 : 0);
  }

  if (useLineShader) {
    uniform1f(locations->lineWidth, lineWidth);
    const SbVec2s & size = frame.viewportSize;
    uniform2f(locations->vpSize, static_cast<float>(size[0]),
              static_cast<float>(size[1]));
    uniform1i(locations->stipplePattern,
              static_cast<GLint>(command.state.raster.linePattern));
    uniform1f(locations->stippleScale,
              static_cast<float>(std::max(1, static_cast<int>(
                command.state.raster.linePatternScale))));
  }
  if (usePointShader) {
    uniform1f(locations->pointSize, pointSize);
    const SbVec2s & size = frame.viewportSize;
    uniform2f(locations->vpSize, static_cast<float>(size[0]),
              static_cast<float>(size[1]));
  }
  if (lineTriangleInput || pointTriangleInput) {
    uniform1f(locations->cullBackFaces,
              command.state.raster.cullBackFaces ? 1.0f : 0.0f);
    uniform1f(locations->frontFaceCCW,
              command.state.raster.frontFaceCCW ? 1.0f : 0.0f);
  }
  if (pixelRaster) {
    const GLsizei stride = static_cast<GLsizei>(
      command.geometry.vertexStride ? command.geometry.vertexStride
                                    : sizeof(float) * 3);
    const char * raw = reinterpret_cast<const char *>(
      command.geometry.positions);
    SbVec3f center(0.0f, 0.0f, 0.0f);
    for (uint32_t i = 0; i < command.geometry.vertexCount; ++i) {
      const float * position = reinterpret_cast<const float *>(raw + i * stride);
      center += SbVec3f(position[0], position[1], position[2]);
    }
    if (command.geometry.vertexCount) {
      center /= static_cast<float>(command.geometry.vertexCount);
    }
    if (locations->quadCenter >= 0) {
      this->glue->glUniform3f(locations->quadCenter,
                              center[0], center[1], center[2]);
    }
    uniform2f(locations->sourceSize,
              static_cast<float>(command.material.texture.width),
              static_cast<float>(command.material.texture.height));
    uniform2f(locations->rasterSize,
              static_cast<float>(command.pixelRaster.width),
              static_cast<float>(command.pixelRaster.height));
    uniform2f(locations->viewportOrigin,
              static_cast<float>(frame.viewportOrigin[0]),
              static_cast<float>(frame.viewportOrigin[1]));
    const SbVec2s & size = frame.viewportSize;
    uniform2f(locations->vpSize, static_cast<float>(size[0]),
              static_cast<float>(size[1]));
    uniform2f(locations->pixelOrigin,
              static_cast<float>(command.pixelRaster.originX),
              static_cast<float>(command.pixelRaster.originY));
    uniform4f(locations->texModColor, SbVec4f(1.0f, 1.0f, 1.0f, 1.0f));
    cc_glglue_glActiveTexture(this->glue, GL_TEXTURE0);
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, cache.texture);
  }
  else if (textured) {
    cc_glglue_glActiveTexture(this->glue, GL_TEXTURE0);
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, cache.texture);
  }

  if (selection) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDepthRange(command.state.depth.range[0], command.state.depth.range[1]);
  }
  else {
    if (command.state.depth.enabled) {
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(depthFunctionToGL(command.state.depth.func));
    }
    else {
      glDisable(GL_DEPTH_TEST);
    }
    // Visual transparent passes suppress depth writes, but the ID pass is a
    // visibility query and must resolve the nearest pickable fragment.
    glDepthMask(GL_TRUE);
    glDepthRange(command.state.depth.range[0], command.state.depth.range[1]);
  }

  const bool triangleFallback = lineTriangleInput || pointTriangleInput;
  if (command.state.raster.cullBackFaces && !triangleFallback) {
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
  }
  else {
    glDisable(GL_CULL_FACE);
  }
  glFrontFace(command.state.raster.frontFaceCCW ? GL_CCW : GL_CW);

  if (!useLineShader && triangleTopology &&
      command.state.raster.fillMode == SO_RASTER_LINES) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  }
  else if (!usePointShader && triangleTopology &&
           command.state.raster.fillMode == SO_RASTER_POINTS) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
  }
  else {
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }

  const bool linePrimitive = primitive == GL_LINES ||
    primitive == GL_LINE_STRIP ||
    command.state.raster.fillMode == SO_RASTER_LINES;
  const bool pointPrimitive = primitive == GL_POINTS ||
    command.state.raster.fillMode == SO_RASTER_POINTS;
  if (linePrimitive && !useLineShader) glLineWidth(lineWidth);
  if (pointPrimitive && !usePointShader) glPointSize(pointSize);

  const bool polygonOffsetApplies =
    ((!linePrimitive && !pointPrimitive) &&
     command.state.raster.polygonOffsetFilled) ||
    (linePrimitive && command.state.raster.polygonOffsetLines) ||
    (pointPrimitive && command.state.raster.polygonOffsetPoints);
  const bool polygonOffset = polygonOffsetApplies &&
    (command.state.raster.polygonOffsetFactor != 0.0f ||
     command.state.raster.polygonOffsetUnits != 0.0f);
  GLenum polygonOffsetTarget = GL_POLYGON_OFFSET_FILL;
  if (useLineShader || usePointShader) {
    polygonOffsetTarget = GL_POLYGON_OFFSET_FILL;
  }
  else if (linePrimitive) polygonOffsetTarget = GL_POLYGON_OFFSET_LINE;
  else if (pointPrimitive) polygonOffsetTarget = GL_POLYGON_OFFSET_POINT;
  if (polygonOffset) {
    glEnable(polygonOffsetTarget);
    glPolygonOffset(command.state.raster.polygonOffsetFactor,
                    command.state.raster.polygonOffsetUnits);
  }

  this->glue->glBindVertexArray(expandedLineStream
                                ? cache.lineRasterVertexArray
                                : cache.vertexArray);
  const bool indexed = command.geometry.indexCount && command.geometry.indices;
  if (expandedLineStream) {
    const uint32_t count = entry.drawCount;
    const uint32_t start = entry.drawStart;
    if (count && start < cache.lineRasterVertexCount &&
        count <= cache.lineRasterVertexCount - start) {
      cc_glglue_glDrawArrays(this->glue, primitive,
                             static_cast<GLint>(start),
                             static_cast<GLsizei>(count));
    }
  }
  else if (indexed) {
    const uint32_t count = entry.drawCount;
    const uint32_t start = entry.drawStart;
    if (count && start < command.geometry.indexCount &&
        count <= command.geometry.indexCount - start) {
      const void * offset = reinterpret_cast<const void *>(
        static_cast<size_t>(start) * sizeof(uint32_t));
      cc_glglue_glDrawElements(this->glue, primitive,
                               static_cast<GLsizei>(count),
                               GL_UNSIGNED_INT, offset);
    }
  }
  else {
    const uint32_t count = entry.drawCount;
    const uint32_t start = entry.drawStart;
    if (count && start < command.geometry.vertexCount &&
        count <= command.geometry.vertexCount - start) {
      cc_glglue_glDrawArrays(this->glue, primitive,
                             static_cast<GLint>(start),
                             static_cast<GLsizei>(count));
    }
  }
  this->glue->glBindVertexArray(0);

  if (polygonOffset) glDisable(polygonOffsetTarget);
  if (textured) cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D, 0);
  cc_glglue_glUseProgram(this->glue, programSet->visual.handle);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDepthRange(0.0, 1.0);
  glFrontFace(GL_CCW);
  if (linePrimitive && !useLineShader) glLineWidth(1.0f);
  if (pointPrimitive && !usePointShader) glPointSize(1.0f);
}

SbBool
SoGLRenderBackend::updatePickBuffer(const SoDrawList & drawlist,
                                    const SoRenderPlan & plan,
                                    const SoRenderParams & params)
{
  if (!this->isInitialized()) {
    this->emitError("updatePickBuffer called before backend initialization");
    return FALSE;
  }
  this->pickTarget.ready = false;
  this->pickTarget.lookup.clear();
  ScopedGLState state(this->glue);
  drawlist.buildPickLUT();
  this->pickTarget.lookup = drawlist.getPickLUT();
  const SbVec2s size = params.viewport.getViewportSizePixels();
  if (!this->ensurePickFramebuffer(size)) return FALSE;

  this->updateGeometryCache(drawlist);

  cc_glglue_glBindFramebuffer(this->glue, GL_FRAMEBUFFER,
                              this->pickTarget.framebuffer);
  cc_glglue_glFramebufferTexture2D(this->glue, GL_FRAMEBUFFER,
                                   GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                   this->pickTarget.depthTextures[0], 0);
  this->pickTarget.activeDepth = 0;
  this->pickTarget.peelEnabled = false;
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glViewport(0, 0, size[0], size[1]);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_TRUE);
  const GLuint zero = 0;
  const GLfloat clearDepth = params.clearDepth;
  this->glue->glClearBufferuiv(GL_COLOR, 0, &zero);
  this->glue->glClearBufferfv(GL_DEPTH, 0, &clearDepth);

  SbMat frameView;
  SbMat frameProjection;
  params.viewMatrix.getValue(frameView);
  params.projMatrix.getValue(frameProjection);
  auto drawPickRange = [&](const SoRenderStage stage,
                           const uint32_t begin,
                           const uint32_t end) {
    std::vector<size_t> entries;
    for (size_t i = 0; i < this->pickTarget.lookup.size(); ++i) {
      const int commandIndex = this->pickTarget.lookup[i].commandIndex;
      if (commandIndex < 0 ||
          static_cast<uint32_t>(commandIndex) < begin ||
          static_cast<uint32_t>(commandIndex) >= end) continue;
      if (drawlist.getCommand(commandIndex).stage != stage) continue;
      entries.push_back(i);
    }
    std::stable_sort(entries.begin(), entries.end(),
      [&drawlist, this](const size_t lhs, const size_t rhs) {
        const SoPickLUTEntry & left = this->pickTarget.lookup[lhs];
        const SoPickLUTEntry & right = this->pickTarget.lookup[rhs];
        return drawlist.getCommand(left.commandIndex).sortKey <
               drawlist.getCommand(right.commandIndex).sortKey;
    });
    for (const size_t index : entries) {
      this->drawPickEntry(drawlist, this->pickTarget.lookup[index],
                          static_cast<GLuint>(index + 1),
                          frameView, frameProjection, params);
    }
  };

  const uint32_t commandCount = static_cast<uint32_t>(drawlist.getNumCommands());
  for (const SoRenderStage stage : { SoRenderStage::Background,
                                     SoRenderStage::Main,
                                     SoRenderStage::AfterMain,
                                     SoRenderStage::Foreground }) {
    uint32_t begin = 0;
    for (const SoDepthClearEvent & event : drawlist.getDepthClearEvents()) {
      if (event.stage != stage) continue;
      const uint32_t barrier = std::min(event.sequence, commandCount);
      drawPickRange(stage, begin, barrier);
      this->clearDepthEvent(event, params, true);
      begin = barrier;
    }
    drawPickRange(stage, begin, commandCount);
  }
  this->pickTarget.generation = drawlist.getPickLUTGeneration();
  this->pickTarget.drawlist = &drawlist;
  this->pickTarget.plan = plan;
  this->pickTarget.params = params;
  this->pickTarget.ready = true;
  return TRUE;
}

SbBool
SoGLRenderBackend::pickClosest(const int x, const int y, const int radius,
                               SoPickResult & result)
{
  result = SoPickResult();
  if (!this->isInitialized() || !this->pickTarget.ready ||
      radius < 0) return FALSE;

  const int width = this->pickTarget.size[0];
  const int height = this->pickTarget.size[1];
  if (width <= 0 || height <= 0) return FALSE;
  const int left = std::max(0, x - radius);
  const int bottom = std::max(0, y - radius);
  const int right = std::min(width - 1, x + radius);
  const int top = std::min(height - 1, y + radius);
  if (left > right || bottom > top) return FALSE;

  ScopedGLState state(this->glue);
  cc_glglue_glBindFramebuffer(this->glue, GL_FRAMEBUFFER,
                              this->pickTarget.framebuffer);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  const int readWidth = right - left + 1;
  const int readHeight = top - bottom + 1;
  const size_t pixelCount = static_cast<size_t>(readWidth) * readHeight;
  std::vector<GLuint> ids(pixelCount, 0);
  std::vector<GLfloat> depths(pixelCount, 1.0f);
  ScopedPixelPackState packState;
  cc_glglue_glBindBuffer(this->glue, GL_PIXEL_PACK_BUFFER, 0);
  glReadPixels(left, bottom, readWidth, readHeight,
               GL_RED_INTEGER, GL_UNSIGNED_INT, ids.data());
  glReadPixels(left, bottom, readWidth, readHeight,
               GL_DEPTH_COMPONENT, GL_FLOAT, depths.data());

  GLuint bestId = 0;
  int bestDistance = 0;
  int bestPixelX = 0;
  int bestPixelY = 0;
  float bestDepth = 1.0f;
  for (int row = 0; row < readHeight; ++row) {
    for (int column = 0; column < readWidth; ++column) {
      const GLuint id = ids[static_cast<size_t>(row) * readWidth + column];
      if (!id) continue;

      const int px = left + column;
      const int py = bottom + row;
      const int dx = px - x;
      const int dy = py - y;
      const int distance = dx * dx + dy * dy;
      if (!bestId || distance < bestDistance) {
        bestId = id;
        bestDistance = distance;
        bestPixelX = px;
        bestPixelY = py;
        bestDepth = depths[static_cast<size_t>(row) * readWidth + column];
      }
    }
  }
  if (!bestId) return FALSE;

  if (bestId == 0 || bestId > this->pickTarget.lookup.size()) return FALSE;
  const SoPickLUTEntry & entry = this->pickTarget.lookup[bestId - 1];
  result.id = bestId;
  result.generation = this->pickTarget.generation;
  result.commandIndex = entry.commandIndex;
  result.nodeId = entry.nodeId;
  result.instanceId = entry.instanceId;
  result.objectId = entry.objectId;
  result.type = entry.type;
  result.elementIndex = entry.elementIndex;
  result.pixelX = bestPixelX;
  result.pixelY = bestPixelY;
  result.depth = bestDepth;
  return TRUE;
}

SbBool
SoGLRenderBackend::pickVisibleRegion(const SbBox2s & region,
                                     SoPickResultList & results)
{
  results = SoPickResultList();
  if (!this->isInitialized() || !this->pickTarget.ready ||
      region.isEmpty()) return FALSE;

  const int width = this->pickTarget.size[0];
  const int height = this->pickTarget.size[1];
  if (width <= 0 || height <= 0) return FALSE;
  const int left = std::max(0, static_cast<int>(region.getMin()[0]));
  const int bottom = std::max(0, static_cast<int>(region.getMin()[1]));
  const int right = std::min(width - 1,
                             static_cast<int>(region.getMax()[0]));
  const int top = std::min(height - 1,
                           static_cast<int>(region.getMax()[1]));
  if (left > right || bottom > top) return FALSE;

  ScopedGLState state(this->glue);
  cc_glglue_glBindFramebuffer(this->glue, GL_FRAMEBUFFER,
                              this->pickTarget.framebuffer);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  const int readWidth = right - left + 1;
  const int readHeight = top - bottom + 1;
  const size_t pixelCount = static_cast<size_t>(readWidth) * readHeight;
  std::vector<GLuint> ids(pixelCount, 0);
  std::vector<GLfloat> depths(pixelCount, 1.0f);
  ScopedPixelPackState packState;
  cc_glglue_glBindBuffer(this->glue, GL_PIXEL_PACK_BUFFER, 0);
  glReadPixels(left, bottom, readWidth, readHeight,
               GL_RED_INTEGER, GL_UNSIGNED_INT, ids.data());
  glReadPixels(left, bottom, readWidth, readHeight,
               GL_DEPTH_COMPONENT, GL_FLOAT, depths.data());

  std::unordered_map<GLuint, SoPickResult> visible;
  for (int row = 0; row < readHeight; ++row) {
    for (int column = 0; column < readWidth; ++column) {
      const size_t offset = static_cast<size_t>(row) * readWidth + column;
      const GLuint id = ids[offset];
      if (!id || id > this->pickTarget.lookup.size()) continue;
      const float depth = depths[offset];
      auto found = visible.find(id);
      if (found != visible.end() && found->second.depth <= depth) continue;

      const SoPickLUTEntry & entry = this->pickTarget.lookup[id - 1];
      SoPickResult hit;
      hit.id = id;
      hit.generation = this->pickTarget.generation;
      hit.commandIndex = entry.commandIndex;
      hit.nodeId = entry.nodeId;
      hit.instanceId = entry.instanceId;
      hit.objectId = entry.objectId;
      hit.type = entry.type;
      hit.elementIndex = entry.elementIndex;
      hit.pixelX = left + column;
      hit.pixelY = bottom + row;
      hit.depth = depth;
      visible[id] = hit;
    }
  }

  results.generation = this->pickTarget.generation;
  results.hits.reserve(visible.size());
  for (const auto & item : visible) results.hits.push_back(item.second);
  std::sort(results.hits.begin(), results.hits.end(),
    [](const SoPickResult & lhs, const SoPickResult & rhs) {
      if (lhs.depth != rhs.depth) return lhs.depth < rhs.depth;
      return lhs.id < rhs.id;
    });
  return !results.hits.empty();
}

SbBool
SoGLRenderBackend::pickDepthStack(const int x, const int y, const int radius,
                                  const int maxLayers, const int maxHits,
                                  SoPickResultList & results)
{
  results = SoPickResultList();
  if (!this->isInitialized() || !this->pickTarget.ready || radius < 0 ||
      maxLayers <= 0 || maxHits <= 0 || !this->pickTarget.drawlist) {
    return FALSE;
  }

  const int width = this->pickTarget.size[0];
  const int height = this->pickTarget.size[1];
  const int left = std::max(0, x - radius);
  const int bottom = std::max(0, y - radius);
  const int right = std::min(width - 1, x + radius);
  const int top = std::min(height - 1, y + radius);
  if (left > right || bottom > top) return FALSE;
  const int readWidth = right - left + 1;
  const int readHeight = top - bottom + 1;
  const size_t pixelCount = static_cast<size_t>(readWidth) * readHeight;

  const SoDrawList & drawlist = *this->pickTarget.drawlist;
  if (drawlist.getGeneration() != this->pickTarget.generation) return FALSE;
  const SoRenderPlan & plan = this->pickTarget.plan;
  const SoRenderParams & params = this->pickTarget.params;
  const uint32_t commandCount = static_cast<uint32_t>(
    drawlist.getNumCommands());

  ScopedGLState state(this->glue);
  ScopedPixelPackState packState;
  cc_glglue_glBindFramebuffer(this->glue, GL_FRAMEBUFFER,
                              this->pickTarget.framebuffer);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  cc_glglue_glBindBuffer(this->glue, GL_PIXEL_PACK_BUFFER, 0);

  auto readLayer = [&]() {
    std::vector<GLuint> ids(pixelCount, 0);
    std::vector<GLfloat> depths(pixelCount, 1.0f);
    glReadPixels(left, bottom, readWidth, readHeight,
                 GL_RED_INTEGER, GL_UNSIGNED_INT, ids.data());
    glReadPixels(left, bottom, readWidth, readHeight,
                 GL_DEPTH_COMPONENT, GL_FLOAT, depths.data());

    std::unordered_map<GLuint, SoPickResult> layerHits;
    for (int row = 0; row < readHeight; ++row) {
      for (int column = 0; column < readWidth; ++column) {
        const size_t offset = static_cast<size_t>(row) * readWidth + column;
        const GLuint id = ids[offset];
        if (!id || id > this->pickTarget.lookup.size()) continue;
        const float depth = depths[offset];
        auto found = layerHits.find(id);
        if (found != layerHits.end() && found->second.depth <= depth) continue;

        const SoPickLUTEntry & entry = this->pickTarget.lookup[id - 1];
        SoPickResult hit;
        hit.id = id;
        hit.generation = this->pickTarget.generation;
        hit.commandIndex = entry.commandIndex;
        hit.nodeId = entry.nodeId;
        hit.instanceId = entry.instanceId;
        hit.objectId = entry.objectId;
        hit.type = entry.type;
        hit.elementIndex = entry.elementIndex;
        hit.pixelX = left + column;
        hit.pixelY = bottom + row;
        hit.depth = depth;
        layerHits[id] = hit;
      }
    }
    std::vector<SoPickResult> ordered;
    ordered.reserve(layerHits.size());
    for (const auto & item : layerHits) ordered.push_back(item.second);
    std::sort(ordered.begin(), ordered.end(),
      [](const SoPickResult & lhs, const SoPickResult & rhs) {
        if (lhs.depth != rhs.depth) return lhs.depth < rhs.depth;
        return lhs.id < rhs.id;
      });
    return ordered;
  };

  auto renderLayer = [&](const int previousDepth, const int targetDepth,
                         const bool peel) {
    cc_glglue_glBindFramebuffer(this->glue, GL_FRAMEBUFFER,
                                this->pickTarget.framebuffer);
    cc_glglue_glFramebufferTexture2D(
      this->glue, GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
      this->pickTarget.depthTextures[targetDepth], 0);
    this->pickTarget.activeDepth = targetDepth;
    this->pickTarget.peelEnabled = peel;

    cc_glglue_glActiveTexture(this->glue, GL_TEXTURE1);
    cc_glglue_glBindTexture(this->glue, GL_TEXTURE_2D,
                            this->pickTarget.depthTextures[previousDepth]);
    cc_glglue_glActiveTexture(this->glue, GL_TEXTURE0);

    glEnable(GL_SCISSOR_TEST);
    glScissor(left, bottom, readWidth, readHeight);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    const GLuint zero = 0;
    const GLfloat clearDepth = params.clearDepth;
    this->glue->glClearBufferuiv(GL_COLOR, 0, &zero);
    this->glue->glClearBufferfv(GL_DEPTH, 0, &clearDepth);

    SbMat frameView;
    SbMat frameProjection;
    params.viewMatrix.getValue(frameView);
    params.projMatrix.getValue(frameProjection);
    auto drawPickCommand = [&](const uint32_t commandIndex) {
      for (size_t i = 0; i < this->pickTarget.lookup.size(); ++i) {
        const SoPickLUTEntry & entry = this->pickTarget.lookup[i];
        if (entry.commandIndex < 0 ||
            static_cast<uint32_t>(entry.commandIndex) != commandIndex) {
          continue;
        }
        this->drawPickEntry(drawlist, entry, static_cast<GLuint>(i + 1),
                            frameView, frameProjection, params);
      }
    };
    for (int i = 0; i < plan.getNumDraws(); ++i) {
      const uint32_t commandIndex = plan.getDraw(i).commandIndex;
      if (commandIndex >= commandCount) return false;
      drawPickCommand(commandIndex);
    }
    return true;
  };

  results.generation = this->pickTarget.generation;
  std::vector<GLuint> seenIds;
  int currentDepth = 0;
  for (int layer = 0; layer <= maxLayers; ++layer) {
    const std::vector<SoPickResult> layerHits = readLayer();
    if (layerHits.empty()) break;
    if (layer == maxLayers) {
      results.truncated = TRUE;
      break;
    }
    for (const SoPickResult & hit : layerHits) {
      if (std::find(seenIds.begin(), seenIds.end(), hit.id) !=
          seenIds.end()) continue;
      if (static_cast<int>(results.hits.size()) >= maxHits) {
        results.truncated = TRUE;
        break;
      }
      seenIds.push_back(hit.id);
      results.hits.push_back(hit);
    }
    if (results.truncated) break;
    const int nextDepth = 1 - currentDepth;
    if (!renderLayer(currentDepth, nextDepth, true)) {
      results.hits.clear();
      results.truncated = FALSE;
      break;
    }
    currentDepth = nextDepth;
  }

  // Restore the frontmost target in the queried region so subsequent hover
  // queries remain cheap and observe the same snapshot.
  renderLayer(1, 0, false);
  this->pickTarget.peelEnabled = false;
  this->pickTarget.activeDepth = 0;
  return !results.hits.empty();
}

SbBool
SoGLRenderBackend::renderSelection(const SoDrawList & drawlist,
                                   const SoSelectionState & selection,
                                   const SoRenderParams & params)
{
  if (!this->isInitialized()) {
    this->emitError("renderSelection called before backend initialization");
    return FALSE;
  }

  ScopedGLState state(this->glue);
  this->updateGeometryCache(drawlist);
  applyViewport(params);
  glEnable(GL_BLEND);
  cc_glglue_glBlendFuncSeparate(this->glue, GL_SRC_ALPHA,
                                GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                                GL_ONE_MINUS_SRC_ALPHA);
  if (cc_glglue_has_blendequation(this->glue)) {
    cc_glglue_glBlendEquation(this->glue, GL_FUNC_ADD);
  }
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);

  SbMat view;
  SbMat projection;
  params.viewMatrix.getValue(view);
  params.projMatrix.getValue(projection);

  auto drawTarget = [&](const SoSelectionTarget & target) {
    if (target.commandIndex < 0 ||
        target.commandIndex >= drawlist.getNumCommands()) return;
    const SoRenderCommand & command = drawlist.getCommand(target.commandIndex);
    if (target.type == SO_PICK_OBJECT && target.elementIndex < 0) {
      SoPickLUTEntry entry;
      entry.commandIndex = target.commandIndex;
      entry.type = SO_PICK_OBJECT;
      const bool indexed = command.geometry.indices != nullptr &&
        command.geometry.indexCount != 0;
      entry.drawCount = indexed ? command.geometry.indexCount
                                : command.geometry.vertexCount;
      this->drawSelectionEntry(drawlist, entry, target.color, view, projection,
                               params);
      return;
    }

    for (const SoRenderElementRange & range : command.pick.elementRanges) {
      if (range.type != target.type ||
          range.elementIndex != target.elementIndex) continue;
      SoPickLUTEntry entry;
      entry.commandIndex = target.commandIndex;
      entry.type = range.type;
      entry.elementIndex = range.elementIndex;
      const bool indexed = command.geometry.indices != nullptr &&
        command.geometry.indexCount != 0;
      const uint32_t drawLimit = indexed ? command.geometry.indexCount
                                         : command.geometry.vertexCount;
      if (range.drawCount == 0 || range.drawStart >= drawLimit ||
          range.drawCount > drawLimit - range.drawStart) continue;
      entry.drawStart = range.drawStart;
      entry.drawCount = range.drawCount;
      this->drawSelectionEntry(drawlist, entry, target.color, view, projection,
                               params);
    }
  };

  for (const SoSelectionTarget & target : selection.selected) {
    drawTarget(target);
  }
  for (const SoSelectionTarget & target : selection.highlighted) {
    drawTarget(target);
  }
  return TRUE;
}

SbBool
SoGLRenderBackend::render(const SoDrawList & drawlist,
                          const SoRenderParams & params)
{
  if (!this->isInitialized()) {
    this->emitError("render called before backend initialization");
    return FALSE;
  }

  this->debugValidateDrawList(drawlist);
  this->beginFrame(params);
  this->updateGeometryCache(drawlist);

  this->renderStage(drawlist, SoRenderStage::Background, params);
  this->renderStage(drawlist, SoRenderStage::Main, params);
  this->renderStage(drawlist, SoRenderStage::AfterMain, params);
  this->renderStage(drawlist, SoRenderStage::Foreground, params);
  cc_glglue_glUseProgram(this->glue, 0);
  return TRUE;
}
