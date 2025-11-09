#include "GLFWBackend.h"

namespace CoinVisualTests {

GLFWBackend::GLFWBackend() = default;
GLFWBackend::~GLFWBackend() = default;

bool GLFWBackend::init(const GLFWBackendConfig& config) {
  config_ = config;
  if (config_.width <= 0 || config_.height <= 0) {
    return false;
  }

  GLTestContextConfig contextConfig;
  contextConfig.profile = GLTestProfile::Compatibility;
  contextConfig.major = 3;
  contextConfig.minor = 3;
  contextConfig.width = config_.width;
  contextConfig.height = config_.height;
  return context_.initialize(contextConfig);
}

void GLFWBackend::bindFramebuffer() {
  context_.bindFramebuffer();
}

bool GLFWBackend::readPixels(std::vector<uint8_t>& output) const {
  output = context_.readPixels();
  return !output.empty();
}

} // namespace CoinVisualTests
