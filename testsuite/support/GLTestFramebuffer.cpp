#include "GLTestFramebuffer.h"

#include <iostream>
#include <Inventor/system/gl.h>
#include <Inventor/C/glue/gl.h>

GLTestFramebuffer::GLTestFramebuffer()
  : glue(NULL),
    framebuffer(0),
    colortexture(0),
    depthrenderbuffer(0),
    framebufferwidth(0),
    framebufferheight(0)
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
  this->glue = glue;
  this->framebufferwidth = width;
  this->framebufferheight = height;

  cc_glglue_glGenFramebuffers(this->glue, 1, &this->framebuffer);
  cc_glglue_glBindFramebuffer(this->glue, GL_FRAMEBUFFER, this->framebuffer);

  glGenTextures(1, &this->colortexture);
  glBindTexture(GL_TEXTURE_2D, this->colortexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
               this->framebufferwidth, this->framebufferheight, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  cc_glglue_glFramebufferTexture2D(this->glue, GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   this->colortexture, 0);

  cc_glglue_glGenRenderbuffers(this->glue, 1, &this->depthrenderbuffer);
  cc_glglue_glBindRenderbuffer(this->glue, GL_RENDERBUFFER,
                               this->depthrenderbuffer);
  cc_glglue_glRenderbufferStorage(this->glue, GL_RENDERBUFFER,
                                  GL_DEPTH_COMPONENT24,
                                  this->framebufferwidth,
                                  this->framebufferheight);
  cc_glglue_glFramebufferRenderbuffer(this->glue, GL_FRAMEBUFFER,
                                      GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                      this->depthrenderbuffer);

  const GLenum status =
    cc_glglue_glCheckFramebufferStatus(this->glue, GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "GL test framebuffer is incomplete: 0x" << std::hex
              << status << std::dec << std::endl;
    cc_glglue_glBindFramebuffer(this->glue, GL_FRAMEBUFFER, 0);
    this->shutdown();
    return false;
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  cc_glglue_glBindRenderbuffer(this->glue, GL_RENDERBUFFER, 0);
  cc_glglue_glBindFramebuffer(this->glue, GL_FRAMEBUFFER, 0);
  return true;
}

void
GLTestFramebuffer::shutdown()
{
  if (this->depthrenderbuffer != 0) {
    cc_glglue_glDeleteRenderbuffers(this->glue, 1, &this->depthrenderbuffer);
    this->depthrenderbuffer = 0;
  }
  if (this->colortexture != 0) {
    glDeleteTextures(1, &this->colortexture);
    this->colortexture = 0;
  }
  if (this->framebuffer != 0) {
    cc_glglue_glDeleteFramebuffers(this->glue, 1, &this->framebuffer);
    this->framebuffer = 0;
  }
  this->framebufferwidth = 0;
  this->framebufferheight = 0;
  this->glue = NULL;
}

void
GLTestFramebuffer::bind() const
{
  cc_glglue_glBindFramebuffer(this->glue, GL_FRAMEBUFFER, this->framebuffer);
  glViewport(0, 0, this->framebufferwidth, this->framebufferheight);
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
  cc_glglue_glBindBuffer(this->glue, GL_PIXEL_PACK_BUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
  glPixelStorei(GL_PACK_SKIP_IMAGES, 0);
  pixels.resize(static_cast<size_t>(this->framebufferwidth) *
                static_cast<size_t>(this->framebufferheight) * 4u);
  glReadPixels(0, 0, this->framebufferwidth, this->framebufferheight,
               GL_RGBA, GL_UNSIGNED_BYTE,
               pixels.data());
  glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
  glPixelStorei(GL_PACK_ROW_LENGTH, packRowLength);
  glPixelStorei(GL_PACK_SKIP_ROWS, packSkipRows);
  glPixelStorei(GL_PACK_SKIP_PIXELS, packSkipPixels);
  glPixelStorei(GL_PACK_IMAGE_HEIGHT, packImageHeight);
  glPixelStorei(GL_PACK_SKIP_IMAGES, packSkipImages);
  cc_glglue_glBindBuffer(this->glue, GL_PIXEL_PACK_BUFFER,
                         static_cast<GLuint>(pixelPackBuffer));
  return pixels;
}
