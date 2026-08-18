#include "rendering/CoinGLReadback.h"

#include "support/GLTestContext.h"

#include <Inventor/system/gl.h>

#include <cstdint>
#include <iostream>

namespace {

int skip(const char * reason)
{
  std::cout << "SKIP: " << reason << std::endl;
  return 77;
}

bool check(bool condition, const char * message)
{
  if (!condition) std::cerr << "FAIL: " << message << std::endl;
  return condition;
}

} // namespace

int main()
{
  GLTestContextConfig config;
  config.profile = GLTestProfile::Core;
  config.major = 3;
  config.minor = 3;
  config.width = 2;
  config.height = 2;

  GLTestContext context;
  if (!context.initialize(config)) {
    return skip("core GLFW OpenGL context is unavailable");
  }
  if (!context.makeCurrent()) {
    return skip("core GLFW OpenGL context could not be made current");
  }
  context.bindFramebuffer();

  glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glPixelStorei(GL_PACK_ALIGNMENT, 8);
  glPixelStorei(GL_PACK_ROW_LENGTH, 7);
  glPixelStorei(GL_PACK_SKIP_ROWS, 3);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 2);
  glPixelStorei(GL_PACK_IMAGE_HEIGHT, 9);
  glPixelStorei(GL_PACK_SKIP_IMAGES, 4);
  while (glGetError() != GL_NO_ERROR) { }

  uint8_t pixels[2 * 2 * 4] = { 0 };
  coin_read_pixels(pixels, SbVec2s(2, 2), 2, 4, FALSE);

  GLint packAlignment;
  GLint packRowLength;
  GLint packSkipRows;
  GLint packSkipPixels;
  GLint packImageHeight;
  GLint packSkipImages;
  glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
  glGetIntegerv(GL_PACK_ROW_LENGTH, &packRowLength);
  glGetIntegerv(GL_PACK_SKIP_ROWS, &packSkipRows);
  glGetIntegerv(GL_PACK_SKIP_PIXELS, &packSkipPixels);
  glGetIntegerv(GL_PACK_IMAGE_HEIGHT, &packImageHeight);
  glGetIntegerv(GL_PACK_SKIP_IMAGES, &packSkipImages);

  int result = 0;
  if (!check(packAlignment == 8, "PACK_ALIGNMENT was not restored")) result = 1;
  if (!check(packRowLength == 7, "PACK_ROW_LENGTH was not restored")) result = 1;
  if (!check(packSkipRows == 3, "PACK_SKIP_ROWS was not restored")) result = 1;
  if (!check(packSkipPixels == 2, "PACK_SKIP_PIXELS was not restored")) result = 1;
  if (!check(packImageHeight == 9, "PACK_IMAGE_HEIGHT was not restored")) result = 1;
  if (!check(packSkipImages == 4, "PACK_SKIP_IMAGES was not restored")) result = 1;
  if (!check(glGetError() == GL_NO_ERROR,
             "readback used compatibility-only pixel-store state")) result = 1;

  for (unsigned int i = 0; i < sizeof(pixels); i += 4) {
    if (!check(pixels[i + 0] >= 60 && pixels[i + 0] <= 70,
               "red readback component is incorrect")) result = 1;
    if (!check(pixels[i + 1] >= 120 && pixels[i + 1] <= 135,
               "green readback component is incorrect")) result = 1;
    if (!check(pixels[i + 2] >= 185 && pixels[i + 2] <= 200,
               "blue readback component is incorrect")) result = 1;
    if (!check(pixels[i + 3] >= 245,
               "alpha readback component is incorrect")) result = 1;
  }
  return result;
}
