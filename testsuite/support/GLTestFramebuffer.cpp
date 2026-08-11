#include "GLTestFramebuffer.h"

#include <iostream>

namespace {

class ScopedPixelPackState {
public:
  ScopedPixelPackState()
  {
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pixelPackBuffer_);
    glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment_);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &packRowLength_);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &packSkipRows_);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &packSkipPixels_);
    glGetIntegerv(GL_PACK_SWAP_BYTES, &packSwapBytes_);
    glGetIntegerv(GL_PACK_LSB_FIRST, &packLSBFirst_);
    glGetIntegerv(GL_PACK_IMAGE_HEIGHT, &packImageHeight_);
    glGetIntegerv(GL_PACK_SKIP_IMAGES, &packSkipImages_);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
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
    glPixelStorei(GL_PACK_ALIGNMENT, packAlignment_);
    glPixelStorei(GL_PACK_ROW_LENGTH, packRowLength_);
    glPixelStorei(GL_PACK_SKIP_ROWS, packSkipRows_);
    glPixelStorei(GL_PACK_SKIP_PIXELS, packSkipPixels_);
    glPixelStorei(GL_PACK_SWAP_BYTES, packSwapBytes_);
    glPixelStorei(GL_PACK_LSB_FIRST, packLSBFirst_);
    glPixelStorei(GL_PACK_IMAGE_HEIGHT, packImageHeight_);
    glPixelStorei(GL_PACK_SKIP_IMAGES, packSkipImages_);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,
                 static_cast<GLuint>(pixelPackBuffer_));
  }

private:
  GLint pixelPackBuffer_ = 0;
  GLint packAlignment_ = 0;
  GLint packRowLength_ = 0;
  GLint packSkipRows_ = 0;
  GLint packSkipPixels_ = 0;
  GLint packSwapBytes_ = 0;
  GLint packLSBFirst_ = 0;
  GLint packImageHeight_ = 0;
  GLint packSkipImages_ = 0;
};

} // namespace

GLTestFramebuffer::GLTestFramebuffer()
  : framebuffer_(0),
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
GLTestFramebuffer::initialize(const int width, const int height)
{
  if (width <= 0 || height <= 0) {
    std::cerr << "Invalid GL test framebuffer size: " << width << "x"
              << height << std::endl;
    return false;
  }

  this->shutdown();
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
}

void
GLTestFramebuffer::bind() const
{
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glViewport(0, 0, width_, height_);
}

std::vector<uint8_t>
GLTestFramebuffer::readPixels() const
{
  std::vector<uint8_t> pixels;
  if (!this->isInitialized()) return pixels;

  this->bind();
  ScopedPixelPackState packState;
  pixels.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4u);
  glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels.data());
  return pixels;
}
