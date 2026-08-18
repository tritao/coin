#ifndef COIN_TEST_GLTESTCONTEXT_H
#define COIN_TEST_GLTESTCONTEXT_H

#include <cstdint>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "GLTestFramebuffer.h"

struct cc_glglue;

enum class GLTestProfile {
  Core,
  Compatibility
};

struct GLTestContextConfig {
  GLTestProfile profile = GLTestProfile::Core;
  int major = 3;
  int minor = 3;
  int width = 64;
  int height = 64;
};

class GLTestContext {
public:
  GLTestContext();
  ~GLTestContext();

  GLTestContext(const GLTestContext &) = delete;
  GLTestContext & operator=(const GLTestContext &) = delete;

  bool initialize(const GLTestContextConfig & config);
  void shutdown();

  bool makeCurrent();
  void bindFramebuffer();
  std::vector<uint8_t> readPixels() const;

  bool isCoreProfile() const { return profile_ == GLTestProfile::Core; }
  int majorVersion() const { return majorVersion_; }
  int minorVersion() const { return minorVersion_; }
  int contextId() const { return contextId_; }
  const cc_glglue * glue() const { return glue_; }
  bool initialized() const { return window_ != NULL && framebuffer_.isInitialized(); }

private:
  GLFWwindow * window_;
  GLTestFramebuffer framebuffer_;
  GLTestProfile profile_;
  int majorVersion_;
  int minorVersion_;
  int contextId_;
  const cc_glglue * glue_;
  bool glfwUser_;
};

#endif // COIN_TEST_GLTESTCONTEXT_H
