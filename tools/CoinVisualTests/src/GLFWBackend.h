#ifndef COIN_VISUAL_TESTS_GLFW_BACKEND_H
#define COIN_VISUAL_TESTS_GLFW_BACKEND_H

#include <cstdint>
#include <vector>

#include "GLTestContext.h"

namespace CoinVisualTests {

struct GLFWBackendConfig {
  int width = 512;
  int height = 512;
};

class GLFWBackend {
public:
  GLFWBackend();
  ~GLFWBackend();

  bool init(const GLFWBackendConfig& config);
  void bindFramebuffer();
  bool readPixels(std::vector<uint8_t>& output) const;
  bool initialized() const { return context_.initialized(); }

private:
  GLFWBackendConfig config_;
  GLTestContext context_;
};

} // namespace CoinVisualTests

#endif
