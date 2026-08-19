#include <Inventor/C/glue/gl.h>

#include "glue/glp.h"
#include "glue/glslp.h"
#include "support/GLTestContext.h"
#include "support/GLTestUtils.h"

#include <iostream>
#include <vector>

namespace {

using coin_test::check;
using coin_test::check_gl_error;
using coin_test::skip;

void print_shader_log(const cc_glglue * glue, GLuint object, SbBool program)
{
  GLint length = 0;
  if (program) {
    cc_glglue_glGetGLSLProgramiv(glue, object, GL_INFO_LOG_LENGTH, &length);
  }
  else {
    cc_glglue_glGetShaderiv(glue, object, GL_INFO_LOG_LENGTH, &length);
  }

  if (length <= 1) return;

  std::vector<char> log(static_cast<size_t>(length), '\0');
  GLsizei written = 0;
  if (program) {
    cc_glglue_glGetProgramInfoLog(glue, object, length, &written, log.data());
  }
  else {
    cc_glglue_glGetShaderInfoLog(glue, object, length, &written, log.data());
  }
  std::cerr << "GLSL log: " << log.data() << std::endl;
}

bool compile_shader(const cc_glglue * glue, GLenum type, const char * source,
                    GLuint & shader)
{
  shader = cc_glglue_glCreateShader(glue, type);
  if (!check(shader != 0, "profile-neutral shader creation failed")) return false;

  const char * sources[] = { source };
  cc_glglue_glShaderSource(glue, shader, 1, sources, NULL);
  cc_glglue_glCompileShader(glue, shader);

  GLint compiled = GL_FALSE;
  cc_glglue_glGetShaderiv(glue, shader, GL_COMPILE_STATUS, &compiled);
  if (!check(compiled == GL_TRUE, "profile-neutral shader compilation failed")) {
    print_shader_log(glue, shader, FALSE);
    return false;
  }
  return true;
}

} // namespace

int main()
{
  GLTestContext context;
  GLTestContextConfig config;
  config.profile = GLTestProfile::Core;
  config.major = 3;
  config.minor = 3;
  config.width = 16;
  config.height = 16;
  if (!context.initialize(config)) {
    return skip("core GLFW OpenGL context is unavailable");
  }
  if (!context.makeCurrent()) {
    return skip("core GLFW OpenGL context could not be made current");
  }

  const cc_glglue * glue = cc_glglue_instance(context.contextId());
  if (glue == NULL) return skip("GL glue instance is unavailable");
  if (!cc_glglue_has_glsl(glue)) {
    return skip("complete GLSL dispatch is unavailable");
  }
  GLuint vertexShader = 0;
  GLuint fragmentShader = 0;
  GLuint program = 0;
  int result = 1;

  do {
    static const char vertexSource[] =
      "#version 330 core\n"
      "uniform float u_scale;\n"
      "void main() { gl_Position = vec4(u_scale, 0.0, 0.0, 1.0); }\n";
    static const char fragmentSource[] =
      "#version 330 core\n"
      "out vec4 coinColor;\n"
      "void main() { coinColor = vec4(1.0); }\n";

    if (!compile_shader(glue, GL_VERTEX_SHADER, vertexSource, vertexShader)) break;
    if (!compile_shader(glue, GL_FRAGMENT_SHADER, fragmentSource, fragmentShader)) break;

    program = cc_glglue_glCreateProgram(glue);
    if (!check(program != 0, "profile-neutral program creation failed")) break;
    cc_glglue_glAttachShader(glue, program, vertexShader);
    cc_glglue_glAttachShader(glue, program, fragmentShader);
    cc_glglue_glLinkProgram(glue, program);

    GLint linked = GL_FALSE;
    cc_glglue_glGetGLSLProgramiv(glue, program, GL_LINK_STATUS, &linked);
    if (!check(linked == GL_TRUE, "profile-neutral program linking failed")) {
      print_shader_log(glue, program, TRUE);
      break;
    }

    cc_glglue_glUseProgram(glue, program);
    if (!check_gl_error("profile-neutral glUseProgram failed")) break;

    const GLint location = cc_glglue_glGetUniformLocation(glue, program, "u_scale");
    if (!check(location >= 0, "profile-neutral uniform lookup failed")) break;
    glue->glUniform1f(location, 1.0f);
    if (!check_gl_error("profile-neutral uniform update failed")) break;

    result = 0;
  } while (false);

  if (program != 0) {
    cc_glglue_glUseProgram(glue, 0);
    cc_glglue_glDeleteProgram(glue, program);
  }
  if (vertexShader != 0) cc_glglue_glDeleteShader(glue, vertexShader);
  if (fragmentShader != 0) cc_glglue_glDeleteShader(glue, fragmentShader);

  return result;
}
