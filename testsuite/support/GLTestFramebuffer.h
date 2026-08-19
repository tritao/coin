#ifndef COIN_TEST_GLTESTFRAMEBUFFER_H
#define COIN_TEST_GLTESTFRAMEBUFFER_H

#include <cstdint>
#include <vector>

#include <Inventor/system/gl.h>

struct cc_glglue;

class GLTestFramebuffer {
public:
  GLTestFramebuffer();
  ~GLTestFramebuffer();

  GLTestFramebuffer(const GLTestFramebuffer &) = delete;
  GLTestFramebuffer & operator=(const GLTestFramebuffer &) = delete;

  bool initialize(const cc_glglue * glue, int width, int height);
  void shutdown();
  void bind() const;

  bool isInitialized() const { return this->framebuffer != 0; }
  int width() const { return this->framebufferwidth; }
  int height() const { return this->framebufferheight; }

  // Returns tightly packed RGBA8 pixels in OpenGL bottom-to-top row order.
  std::vector<uint8_t> readPixels() const;

private:
  const cc_glglue * glue;
  GLuint framebuffer;
  GLuint colortexture;
  GLuint depthrenderbuffer;
  int framebufferwidth;
  int framebufferheight;
};

#endif // COIN_TEST_GLTESTFRAMEBUFFER_H
