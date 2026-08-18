#include "GLTestFramebuffer.h"

#include <iostream>
#include <Inventor/system/gl.h>
#include <Inventor/C/glue/gl.h>

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
GLTestFramebuffer::initialize(const cc_glglue * glue, const int width,
                              const int height)
{
  if (glue == NULL || width <= 0 || height <= 0) {
    std::cerr << "Invalid GL test framebuffer size: " << width << "x"
              << height << std::endl;
    return false;
  }

  this->shutdown();
  glue_ = glue;
  width_ = width;
  height_ = height;

  cc_glglue_glGenFramebuffers(glue_, 1, &framebuffer_);
  cc_glglue_glBindFramebuffer(glue_, GL_FRAMEBUFFER, framebuffer_);

  glGenTextures(1, &colorTexture_);
  glBindTexture(GL_TEXTURE_2D, colorTexture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  cc_glglue_glFramebufferTexture2D(glue_, GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   colorTexture_, 0);

  cc_glglue_glGenRenderbuffers(glue_, 1, &depthRenderbuffer_);
  cc_glglue_glBindRenderbuffer(glue_, GL_RENDERBUFFER, depthRenderbuffer_);
  cc_glglue_glRenderbufferStorage(glue_, GL_RENDERBUFFER,
                                  GL_DEPTH_COMPONENT24, width_, height_);
  cc_glglue_glFramebufferRenderbuffer(glue_, GL_FRAMEBUFFER,
                                      GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                      depthRenderbuffer_);

  const GLenum status = cc_glglue_glCheckFramebufferStatus(glue_, GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "GL test framebuffer is incomplete: 0x" << std::hex
              << status << std::dec << std::endl;
    cc_glglue_glBindFramebuffer(glue_, GL_FRAMEBUFFER, 0);
    this->shutdown();
    return false;
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  cc_glglue_glBindRenderbuffer(glue_, GL_RENDERBUFFER, 0);
  cc_glglue_glBindFramebuffer(glue_, GL_FRAMEBUFFER, 0);
  return true;
}

void
GLTestFramebuffer::shutdown()
{
  if (depthRenderbuffer_ != 0) {
    cc_glglue_glDeleteRenderbuffers(glue_, 1, &depthRenderbuffer_);
    depthRenderbuffer_ = 0;
  }
  if (colorTexture_ != 0) {
    glDeleteTextures(1, &colorTexture_);
    colorTexture_ = 0;
  }
  if (framebuffer_ != 0) {
    cc_glglue_glDeleteFramebuffers(glue_, 1, &framebuffer_);
    framebuffer_ = 0;
  }
  width_ = 0;
  height_ = 0;
  glue_ = NULL;
}

void
GLTestFramebuffer::bind() const
{
  cc_glglue_glBindFramebuffer(glue_, GL_FRAMEBUFFER, framebuffer_);
  glViewport(0, 0, width_, height_);
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
  cc_glglue_glBindBuffer(glue_, GL_PIXEL_PACK_BUFFER, 0);
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
  cc_glglue_glBindBuffer(glue_, GL_PIXEL_PACK_BUFFER,
                         static_cast<GLuint>(pixelPackBuffer));
  return pixels;
}
