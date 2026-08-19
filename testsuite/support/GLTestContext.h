#ifndef COIN_TEST_GLTESTCONTEXT_H
#define COIN_TEST_GLTESTCONTEXT_H

#include <cstdint>
#include <vector>

#include "GLTestFramebuffer.h"

struct cc_glglue;
struct GLFWwindow;

enum class GLTestProfile {
  Core,
  Compatibility
};

struct GLTestContextConfig {
  // Shared GL integration tests intentionally require modern (3.x+) contexts.
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

  bool isCoreProfile() const { return this->profile == GLTestProfile::Core; }
  int majorVersion() const { return this->majorversion; }
  int minorVersion() const { return this->minorversion; }
  int contextId() const { return this->contextid; }
  const cc_glglue * glue() const { return this->glueinstance; }
  bool initialized() const {
    return this->window != nullptr && this->framebuffer.isInitialized();
  }

private:
  GLFWwindow * window;
  GLTestFramebuffer framebuffer;
  GLTestProfile profile;
  int majorversion;
  int minorversion;
  int contextid;
  const cc_glglue * glueinstance;
  bool glfwuser;
};

#endif // COIN_TEST_GLTESTCONTEXT_H
