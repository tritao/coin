#include "GLTestContext.h"

#include <iostream>

#include <Inventor/system/gl.h>
#include <Inventor/C/glue/gl.h>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

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
  : window(NULL),
    profile(GLTestProfile::Core),
    majorversion(0),
    minorversion(0),
    contextid(0),
    glueinstance(NULL),
    glfwuser(false)
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
  if (config.major < 3) {
    std::cerr << "GLTestContext requires OpenGL 3.0 or newer" << std::endl;
    return false;
  }
  if (config.minor < 0 || config.width <= 0 || config.height <= 0) {
    std::cerr << "Invalid GL test context configuration" << std::endl;
    return false;
  }

  glfwSetErrorCallback(glfwErrorCallback);
  if (glfwUsers == 0 && !glfwInit()) {
    std::cerr << "GLFW initialization failed" << std::endl;
    return false;
  }
  ++glfwUsers;
  this->glfwuser = true;

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

  this->window = glfwCreateWindow(config.width, config.height,
                                  "Coin GL test", NULL, NULL);
  if (this->window == NULL) {
    std::cerr << "Unable to create requested "
              << (config.profile == GLTestProfile::Core ? "core" : "compatibility")
              << " OpenGL context" << std::endl;
    this->shutdown();
    return false;
  }

  this->profile = config.profile;
  glfwMakeContextCurrent(this->window);
  glfwSwapInterval(0);

  this->contextid = nextContextId++;
  this->glueinstance = cc_glglue_instance(this->contextid);
  if (this->glueinstance == NULL) {
    std::cerr << "Unable to initialize Coin GL glue" << std::endl;
    this->shutdown();
    return false;
  }

  unsigned int glueMajor = 0;
  unsigned int glueMinor = 0;
  unsigned int glueRelease = 0;
  cc_glglue_glversion(this->glueinstance, &glueMajor, &glueMinor, &glueRelease);
  this->majorversion = static_cast<int>(glueMajor);
  this->minorversion = static_cast<int>(glueMinor);
  if (!versionAtLeast(this->majorversion, this->minorversion,
                      config.major, config.minor)) {
    std::cerr << "GL test context version " << this->majorversion << "."
              << this->minorversion << " is below requested " << config.major
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

  if (!this->framebuffer.initialize(this->glueinstance,
                                    config.width, config.height)) {
    this->shutdown();
    return false;
  }
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
  if (this->window != NULL) {
    glfwMakeContextCurrent(this->window);
    this->framebuffer.shutdown();
    glfwDestroyWindow(this->window);
    this->window = NULL;
  }
  else {
    this->framebuffer.shutdown();
  }

  if (this->glfwuser) {
    --glfwUsers;
    this->glfwuser = false;
    if (glfwUsers == 0) glfwTerminate();
  }
  this->majorversion = 0;
  this->minorversion = 0;
  this->contextid = 0;
  this->glueinstance = NULL;
}

bool
GLTestContext::makeCurrent()
{
  if (this->window == NULL) return false;
  glfwMakeContextCurrent(this->window);
  return glfwGetCurrentContext() == this->window;
}

void
GLTestContext::bindFramebuffer()
{
  if (!this->makeCurrent()) return;
  this->framebuffer.bind();
}

std::vector<uint8_t>
GLTestContext::readPixels() const
{
  if (this->window == NULL) return std::vector<uint8_t>();
  glfwMakeContextCurrent(this->window);
  return this->framebuffer.readPixels();
}
