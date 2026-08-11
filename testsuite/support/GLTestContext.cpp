#include "GLTestContext.h"

#include <iostream>

#include <Inventor/system/gl.h>

namespace {

int glfwUsers = 0;
int nextContextId = 1000;

void glfwErrorCallback(const int code, const char * description)
{
  std::cerr << "GLFW error " << code << ": "
            << (description ? description : "unknown error") << std::endl;
}

bool versionAtLeast(const int actualMajor, const int actualMinor,
                   const int requestedMajor, const int requestedMinor)
{
  return actualMajor > requestedMajor ||
    (actualMajor == requestedMajor && actualMinor >= requestedMinor);
}

} // namespace

GLTestContext::GLTestContext()
  : window_(NULL),
    profile_(GLTestProfile::Core),
    majorVersion_(0),
    minorVersion_(0),
    contextId_(0),
    glfwUser_(false)
{
}

GLTestContext::~GLTestContext()
{
  this->shutdown();
}

bool
GLTestContext::initialize(const GLTestContextConfig & config)
{
  if (this->initialized()) return true;
  if (config.major < 3 || config.minor < 0 ||
      config.width <= 0 || config.height <= 0) {
    std::cerr << "Invalid GL test context configuration" << std::endl;
    return false;
  }

  glfwSetErrorCallback(glfwErrorCallback);
  if (glfwUsers == 0 && !glfwInit()) {
    std::cerr << "GLFW initialization failed" << std::endl;
    return false;
  }
  ++glfwUsers;
  glfwUser_ = true;

  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, config.major);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, config.minor);
  glfwWindowHint(GLFW_OPENGL_PROFILE,
                 config.profile == GLTestProfile::Core
                   ? GLFW_OPENGL_CORE_PROFILE
                   : GLFW_OPENGL_COMPAT_PROFILE);
#ifdef __APPLE__
  if (config.profile == GLTestProfile::Core) {
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  }
#endif

  window_ = glfwCreateWindow(config.width, config.height,
                             "Coin GL test", NULL, NULL);
  if (window_ == NULL) {
    std::cerr << "Unable to create requested "
              << (config.profile == GLTestProfile::Core ? "core" : "compatibility")
              << " OpenGL context" << std::endl;
    this->shutdown();
    return false;
  }

  profile_ = config.profile;
  glfwMakeContextCurrent(window_);
  glfwSwapInterval(0);

  GLint major = 0;
  GLint minor = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &major);
  glGetIntegerv(GL_MINOR_VERSION, &minor);
  majorVersion_ = static_cast<int>(major);
  minorVersion_ = static_cast<int>(minor);
  if (!versionAtLeast(majorVersion_, minorVersion_, config.major, config.minor)) {
    std::cerr << "GL test context version " << majorVersion_ << "."
              << minorVersion_ << " is below requested " << config.major
              << "." << config.minor << std::endl;
    this->shutdown();
    return false;
  }

#ifdef GL_CONTEXT_PROFILE_MASK
  GLint profileMask = 0;
  glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
  const GLint expectedMask = config.profile == GLTestProfile::Core
    ? GL_CONTEXT_CORE_PROFILE_BIT : GL_CONTEXT_COMPATIBILITY_PROFILE_BIT;
  if ((profileMask & expectedMask) == 0) {
    std::cerr << "GL test context has an unexpected profile" << std::endl;
    this->shutdown();
    return false;
  }
#endif

  if (!framebuffer_.initialize(config.width, config.height)) {
    this->shutdown();
    return false;
  }
  contextId_ = nextContextId++;
  this->bindFramebuffer();
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  return true;
}

void
GLTestContext::shutdown()
{
  if (window_ != NULL) {
    glfwMakeContextCurrent(window_);
    framebuffer_.shutdown();
    glfwDestroyWindow(window_);
    window_ = NULL;
  }
  else {
    framebuffer_.shutdown();
  }

  if (glfwUser_) {
    --glfwUsers;
    glfwUser_ = false;
    if (glfwUsers == 0) glfwTerminate();
  }
  majorVersion_ = 0;
  minorVersion_ = 0;
  contextId_ = 0;
}

bool
GLTestContext::makeCurrent()
{
  if (window_ == NULL) return false;
  glfwMakeContextCurrent(window_);
  return glfwGetCurrentContext() == window_;
}

void
GLTestContext::bindFramebuffer()
{
  if (!this->makeCurrent()) return;
  framebuffer_.bind();
}

std::vector<uint8_t>
GLTestContext::readPixels() const
{
  if (window_ == NULL) return std::vector<uint8_t>();
  glfwMakeContextCurrent(window_);
  return framebuffer_.readPixels();
}
