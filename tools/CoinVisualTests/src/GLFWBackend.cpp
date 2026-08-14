#include "GLFWBackend.h"

#include <iostream>

namespace CoinVisualTests {

GLFWBackend::GLFWBackend() = default;

GLFWBackend::~GLFWBackend() {
  if (color_texture_) {
    glDeleteTextures(1, &color_texture_);
  }
  if (depth_renderbuffer_) {
    glDeleteRenderbuffers(1, &depth_renderbuffer_);
  }
  if (framebuffer_) {
    glDeleteFramebuffers(1, &framebuffer_);
  }
  if (window_) {
    glfwDestroyWindow(window_);
    glfwTerminate();
  }
}

bool GLFWBackend::init(const GLFWBackendConfig& config) {
  if (initialized_) {
    return true;
  }

  config_ = config;
  if (config_.width <= 0 || config_.height <= 0) {
    std::cerr << "Invalid framebuffer size: " << config_.width << "x" << config_.height << '\n';
    return false;
  }
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW.\n";
    return false;
  }

  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  if (config_.profile == OpenGLProfile::Core) {
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  } else {
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
  }

  window_ = glfwCreateWindow(config_.width, config_.height, "CoinVisualTests", nullptr, nullptr);
  if (!window_) {
    std::cerr << "Failed to create GLFW compatibility context.\n";
    glfwTerminate();
    return false;
  }
  glfwMakeContextCurrent(window_);
  glfwSwapInterval(0);

  if (!this->validateContext()) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
    glfwTerminate();
    return false;
  }

  glGenFramebuffers(1, &framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);

  glGenTextures(1, &color_texture_);
  glBindTexture(GL_TEXTURE_2D, color_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, config_.width, config_.height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texture_, 0);

  glGenRenderbuffers(1, &depth_renderbuffer_);
  glBindRenderbuffer(GL_RENDERBUFFER, depth_renderbuffer_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, config_.width, config_.height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_renderbuffer_);

  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "Framebuffer incomplete: " << status << '\n';
    return false;
  }

  initialized_ = true;
  return true;
}

bool GLFWBackend::validateContext() const {
  GLint major = 0;
  GLint minor = 0;
  GLint profile_mask = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &major);
  glGetIntegerv(GL_MINOR_VERSION, &minor);
  glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile_mask);

  const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
  const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const char* shading_language = reinterpret_cast<const char*>(
    glGetString(GL_SHADING_LANGUAGE_VERSION));
  const bool core = (profile_mask & GL_CONTEXT_CORE_PROFILE_BIT) != 0;
  const bool compatibility =
    (profile_mask & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) != 0;
  const char* actual_profile = core ? "core" : compatibility ? "compatibility" : "unknown";

  std::cerr << "[CoinVisualTests] OpenGL context: version="
            << (version ? version : "<unknown>")
            << " profile=" << actual_profile
            << " vendor=" << (vendor ? vendor : "<unknown>")
            << " renderer=" << (renderer ? renderer : "<unknown>")
            << " glsl=" << (shading_language ? shading_language : "<unknown>")
            << '\n';

  const bool version_ok = major > 3 || (major == 3 && minor >= 3);
  const bool profile_ok = config_.profile == OpenGLProfile::Core
    ? core : compatibility;
  if (!version_ok || !profile_ok) {
    std::cerr << "Requested OpenGL 3.3 "
              << (config_.profile == OpenGLProfile::Core ? "core" : "compatibility")
              << " context was not provided.\n";
    return false;
  }
  return true;
}

void GLFWBackend::bindFramebuffer() {
  if (!initialized_) {
    return;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glViewport(0, 0, config_.width, config_.height);
}

bool GLFWBackend::readPixels(std::vector<uint8_t>& output) const {
  if (!initialized_) {
    return false;
  }
  const size_t stride = static_cast<size_t>(config_.width) * 4;
  output.resize(static_cast<size_t>(config_.height) * stride);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, config_.width, config_.height, GL_RGBA, GL_UNSIGNED_BYTE, output.data());
  return true;
}

} // namespace CoinVisualTests
