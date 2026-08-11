#include "rendering/CoinOffscreenGLCanvas.h"
#include "support/GLTestUtils.h"

#include <Inventor/SoDB.h>
#include <Inventor/system/gl.h>

#include <cstdlib>
namespace {

using coin_test::skip;

void set_environment(const char * name, const char * value)
{
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

} // namespace

int main()
{
  set_environment("COIN_EGL", "1");
  set_environment("EGL_PLATFORM", "surfaceless");
  set_environment("COIN_EGL_CORE_PROFILE", "1");
  SoDB::init();

  CoinOffscreenGLCanvas canvas;
  canvas.setWantedSize(SbVec2s(2, 2));
  if (canvas.activateGLContext() == 0) {
    SoDB::finish();
    return skip("Coin's EGL offscreen context is unavailable");
  }

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  canvas.deactivateGLContext();
  SoDB::finish();
  return 0;
}
