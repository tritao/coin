#include "GLTestFramebuffer.h"

#include <iostream>

#include <Inventor/C/glue/gl.h>

#ifndef APIENTRY
#define APIENTRY
#endif

namespace {

typedef void (APIENTRY * CoinTestBlitFramebufferProc)(
  GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
  GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
  GLbitfield mask, GLenum filter);

CoinTestBlitFramebufferProc
getBlitFramebufferProc(const cc_glglue * glue)
{
  CoinTestBlitFramebufferProc proc =
    reinterpret_cast<CoinTestBlitFramebufferProc>(
      cc_glglue_getprocaddress(glue, "glBlitFramebuffer"));
  if (proc == NULL) {
    proc = reinterpret_cast<CoinTestBlitFramebufferProc>(
      cc_glglue_getprocaddress(glue, "glBlitFramebufferEXT"));
  }
  return proc;
}

} // namespace

GLTestFramebuffer::GLTestFramebuffer()
  : glue_(NULL),
    framebuffer_(0),
    colorTexture_(0),
    depthRenderbuffer_(0),
    width_(0),
    height_(0)
{
}

GLTestFramebuffer::~GLTestFramebuffer()
{
  this->shutdown();
}

bool
GLTestFramebuffer::initialize(const cc_glglue * glue,
                              const int width, const int height)
{
  if (width <= 0 || height <= 0) {
    std::cerr << "Invalid GL test framebuffer size: " << width << "x"
              << height << std::endl;
    return false;
  }

  this->shutdown();
  glue_ = glue;
  width_ = width;
  height_ = height;

  glGenFramebuffers(1, &framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);

  glGenTextures(1, &colorTexture_);
  glBindTexture(GL_TEXTURE_2D, colorTexture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, colorTexture_, 0);

  glGenRenderbuffers(1, &depthRenderbuffer_);
  glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                        width_, height_);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, depthRenderbuffer_);

  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "GL test framebuffer is incomplete: 0x" << std::hex
              << status << std::dec << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    this->shutdown();
    return false;
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

void
GLTestFramebuffer::shutdown()
{
  if (depthRenderbuffer_ != 0) {
    glDeleteRenderbuffers(1, &depthRenderbuffer_);
    depthRenderbuffer_ = 0;
  }
  if (colorTexture_ != 0) {
    glDeleteTextures(1, &colorTexture_);
    colorTexture_ = 0;
  }
  if (framebuffer_ != 0) {
    glDeleteFramebuffers(1, &framebuffer_);
    framebuffer_ = 0;
  }
  width_ = 0;
  height_ = 0;
  glue_ = NULL;
}

void
GLTestFramebuffer::bind() const
{
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glViewport(0, 0, width_, height_);
}

void
GLTestFramebuffer::blitToDefault() const
{
  if (!this->isInitialized()) return;
  cc_glglue_glBindFramebuffer(glue_, GL_READ_FRAMEBUFFER, framebuffer_);
  cc_glglue_glBindFramebuffer(glue_, GL_DRAW_FRAMEBUFFER, 0);
  CoinTestBlitFramebufferProc proc = getBlitFramebufferProc(glue_);
  if (proc != NULL) {
    proc(0, 0, width_, height_, 0, 0, width_, height_,
         GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }
}

std::vector<uint8_t>
GLTestFramebuffer::readPixels() const
{
  std::vector<uint8_t> pixels;
  if (!this->isInitialized()) return pixels;

  this->bind();
  GLint packAlignment = 0;
  GLint packRowLength = 0;
  GLint packSkipRows = 0;
  GLint packSkipPixels = 0;
  GLint packImageHeight = 0;
  GLint packSkipImages = 0;
  GLint pixelPackBuffer = 0;
  glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
  glGetIntegerv(GL_PACK_ROW_LENGTH, &packRowLength);
  glGetIntegerv(GL_PACK_SKIP_ROWS, &packSkipRows);
  glGetIntegerv(GL_PACK_SKIP_PIXELS, &packSkipPixels);
  glGetIntegerv(GL_PACK_IMAGE_HEIGHT, &packImageHeight);
  glGetIntegerv(GL_PACK_SKIP_IMAGES, &packSkipImages);
  glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pixelPackBuffer);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
  glPixelStorei(GL_PACK_SKIP_IMAGES, 0);
  pixels.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4u);
  glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels.data());
  glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
  glPixelStorei(GL_PACK_ROW_LENGTH, packRowLength);
  glPixelStorei(GL_PACK_SKIP_ROWS, packSkipRows);
  glPixelStorei(GL_PACK_SKIP_PIXELS, packSkipPixels);
  glPixelStorei(GL_PACK_IMAGE_HEIGHT, packImageHeight);
  glPixelStorei(GL_PACK_SKIP_IMAGES, packSkipImages);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(pixelPackBuffer));
  return pixels;
}
