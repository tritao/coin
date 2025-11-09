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
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

  window_ = glfwCreateWindow(config_.width, config_.height, "CoinVisualTests", nullptr, nullptr);
  if (!window_) {
    std::cerr << "Failed to create GLFW compatibility context.\n";
    glfwTerminate();
    return false;
  }
  glfwMakeContextCurrent(window_);
  glfwSwapInterval(0);

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
