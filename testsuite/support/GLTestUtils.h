#ifndef COIN_TEST_GLTESTUTILS_H
#define COIN_TEST_GLTESTUTILS_H

#include <iostream>

#include <Inventor/system/gl.h>

#include "TestUtils.h"

namespace coin_test {

inline bool
check_gl_error(const char * message)
{
  const GLenum error = glGetError();
  if (error == GL_NO_ERROR) return true;
  std::cerr << "FAIL: " << message << " (0x" << std::hex << error
            << std::dec << ")" << std::endl;
  return false;
}

} // namespace coin_test

#endif // COIN_TEST_GLTESTUTILS_H
