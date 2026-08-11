#ifndef COIN_TEST_GLTESTFRAMEBUFFER_H
#define COIN_TEST_GLTESTFRAMEBUFFER_H

#include <cstdint>
#include <vector>

#include <Inventor/system/gl.h>

class GLTestFramebuffer {
public:
  GLTestFramebuffer();
  ~GLTestFramebuffer();

  GLTestFramebuffer(const GLTestFramebuffer &) = delete;
  GLTestFramebuffer & operator=(const GLTestFramebuffer &) = delete;

  bool initialize(int width, int height);
  void shutdown();
  void bind() const;

  bool isInitialized() const { return framebuffer_ != 0; }
  int width() const { return width_; }
  int height() const { return height_; }

  std::vector<uint8_t> readPixels() const;

private:
  GLuint framebuffer_;
  GLuint colorTexture_;
  GLuint depthRenderbuffer_;
  int width_;
  int height_;
};

#endif // COIN_TEST_GLTESTFRAMEBUFFER_H
