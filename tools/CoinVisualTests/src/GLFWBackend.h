#ifndef COIN_VISUAL_TESTS_GLFW_BACKEND_H
#define COIN_VISUAL_TESTS_GLFW_BACKEND_H

#include <cstdint>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <Inventor/system/gl.h>

#include "RenderDriver.h"

namespace CoinVisualTests {

struct GLFWBackendConfig {
  int width = 512;
  int height = 512;
  OpenGLProfile profile = OpenGLProfile::Compatibility;
};

class GLFWBackend {
public:
  GLFWBackend();
  ~GLFWBackend();

  bool init(const GLFWBackendConfig& config);
  void bindFramebuffer();
  bool readPixels(std::vector<uint8_t>& output) const;
  bool initialized() const { return initialized_; }

private:
  bool validateContext() const;

  GLFWBackendConfig config_;
  GLFWwindow* window_ = nullptr;
  GLuint framebuffer_ = 0;
  GLuint color_texture_ = 0;
  GLuint depth_renderbuffer_ = 0;
  bool initialized_ = false;
};

} // namespace CoinVisualTests

#endif
