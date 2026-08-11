#include "glue/glslp.h"

#include "glue/glp.h"

SbBool
cc_glglue_has_glsl(const cc_glglue * glue)
{
  if (glue == NULL) return FALSE;

  return
    (glue->glCreateShader || glue->glCreateShaderObjectARB) &&
    (glue->glShaderSource || glue->glShaderSourceARB) &&
    (glue->glCompileShader || glue->glCompileShaderARB) &&
    (glue->glGetShaderiv || glue->glGetObjectParameterivARB) &&
    (glue->glGetShaderInfoLog || glue->glGetInfoLogARB) &&
    (glue->glDeleteShader || glue->glDeleteObjectARB) &&
    (glue->glCreateProgram || glue->glCreateProgramObjectARB) &&
    (glue->glAttachShader || glue->glAttachObjectARB) &&
    (glue->glDetachShader || glue->glDetachObjectARB) &&
    (glue->glLinkProgram || glue->glLinkProgramARB) &&
    (glue->glUseProgram || glue->glUseProgramObjectARB) &&
    (glue->glDeleteProgram || glue->glDeleteObjectARB) &&
    (glue->glGetProgramiv || glue->glGetObjectParameterivARB) &&
    (glue->glGetProgramInfoLog || glue->glGetInfoLogARB) &&
    (glue->glGetUniformLocation || glue->glGetUniformLocationARB) &&
    (glue->glGetActiveUniform || glue->glGetActiveUniformARB) &&
    glue->glUniform1f && glue->glUniform2f && glue->glUniform3f &&
    glue->glUniform4f && glue->glUniform1fv && glue->glUniform2fv &&
    glue->glUniform3fv && glue->glUniform4fv && glue->glUniform1i &&
    glue->glUniform2i && glue->glUniform3i && glue->glUniform4i &&
    glue->glUniform1iv && glue->glUniform2iv && glue->glUniform3iv &&
    glue->glUniform4iv && glue->glUniformMatrix4fv;
}

GLuint
cc_glglue_glCreateShader(const cc_glglue * glue, GLenum type)
{
  if (glue->glCreateShader) return glue->glCreateShader(type);
  if (glue->glCreateShaderObjectARB) {
    return (GLuint) glue->glCreateShaderObjectARB(type);
  }
  return 0;
}

void
cc_glglue_glShaderSource(const cc_glglue * glue, GLuint shader, GLsizei count,
                         const char * const * string, const GLint * length)
{
  if (glue->glShaderSource) {
    glue->glShaderSource(shader, count, string, length);
  }
  else if (glue->glShaderSourceARB) {
    glue->glShaderSourceARB((COIN_GLhandle) shader, count,
                            (const COIN_GLchar **) string, length);
  }
}

void
cc_glglue_glCompileShader(const cc_glglue * glue, GLuint shader)
{
  if (glue->glCompileShader) glue->glCompileShader(shader);
  else if (glue->glCompileShaderARB) glue->glCompileShaderARB((COIN_GLhandle) shader);
}

void
cc_glglue_glGetShaderiv(const cc_glglue * glue, GLuint shader, GLenum pname,
                        GLint * params)
{
  if (params != NULL) *params = 0;
  if (glue->glGetShaderiv) glue->glGetShaderiv(shader, pname, params);
  else if (glue->glGetObjectParameterivARB) {
    glue->glGetObjectParameterivARB((COIN_GLhandle) shader, pname, params);
  }
}

void
cc_glglue_glGetShaderInfoLog(const cc_glglue * glue, GLuint shader,
                             GLsizei maxLength, GLsizei * length, char * infoLog)
{
  if (length != NULL) *length = 0;
  if (infoLog != NULL && maxLength > 0) infoLog[0] = '\0';
  if (glue->glGetShaderInfoLog) {
    glue->glGetShaderInfoLog(shader, maxLength, length, infoLog);
  }
  else if (glue->glGetInfoLogARB) {
    glue->glGetInfoLogARB((COIN_GLhandle) shader, maxLength, length,
                          (COIN_GLchar *) infoLog);
  }
}

void
cc_glglue_glDeleteShader(const cc_glglue * glue, GLuint shader)
{
  if (glue->glDeleteShader) glue->glDeleteShader(shader);
  else if (glue->glDeleteObjectARB) glue->glDeleteObjectARB((COIN_GLhandle) shader);
}

void
cc_glglue_glAttachShader(const cc_glglue * glue, GLuint program, GLuint shader)
{
  if (glue->glAttachShader) glue->glAttachShader(program, shader);
  else if (glue->glAttachObjectARB) {
    glue->glAttachObjectARB((COIN_GLhandle) program, (COIN_GLhandle) shader);
  }
}

void
cc_glglue_glDetachShader(const cc_glglue * glue, GLuint program, GLuint shader)
{
  if (glue->glDetachShader) glue->glDetachShader(program, shader);
  else if (glue->glDetachObjectARB) {
    glue->glDetachObjectARB((COIN_GLhandle) program, (COIN_GLhandle) shader);
  }
}

GLint
cc_glglue_glGetUniformLocation(const cc_glglue * glue, GLuint program,
                               const char * name)
{
  if (glue->glGetUniformLocation) return glue->glGetUniformLocation(program, name);
  if (glue->glGetUniformLocationARB) {
    return glue->glGetUniformLocationARB((COIN_GLhandle) program,
                                         (const COIN_GLchar *) name);
  }
  return -1;
}

void
cc_glglue_glGetActiveUniform(const cc_glglue * glue, GLuint program, GLuint index,
                             GLsizei maxLength, GLsizei * length, GLint * size,
                             GLenum * type, char * name)
{
  if (length != NULL) *length = 0;
  if (size != NULL) *size = 0;
  if (type != NULL) *type = 0;
  if (name != NULL && maxLength > 0) name[0] = '\0';
  if (glue->glGetActiveUniform) {
    glue->glGetActiveUniform(program, index, maxLength, length, size, type, name);
  }
  else if (glue->glGetActiveUniformARB) {
    glue->glGetActiveUniformARB((COIN_GLhandle) program, index, maxLength,
                                length, size, type, (COIN_GLchar *) name);
  }
}

GLuint
cc_glglue_glCreateProgram(const cc_glglue * glue)
{
  if (glue->glCreateProgram) return glue->glCreateProgram();
  if (glue->glCreateProgramObjectARB) return (GLuint) glue->glCreateProgramObjectARB();
  return 0;
}

void
cc_glglue_glLinkProgram(const cc_glglue * glue, GLuint program)
{
  if (glue->glLinkProgram) glue->glLinkProgram(program);
  else if (glue->glLinkProgramARB) glue->glLinkProgramARB((COIN_GLhandle) program);
}

void
cc_glglue_glUseProgram(const cc_glglue * glue, GLuint program)
{
  if (glue->glUseProgram) glue->glUseProgram(program);
  else if (glue->glUseProgramObjectARB) glue->glUseProgramObjectARB((COIN_GLhandle) program);
}

void
cc_glglue_glDeleteProgram(const cc_glglue * glue, GLuint program)
{
  if (glue->glDeleteProgram) glue->glDeleteProgram(program);
  else if (glue->glDeleteObjectARB) glue->glDeleteObjectARB((COIN_GLhandle) program);
}

void
cc_glglue_glGetGLSLProgramiv(const cc_glglue * glue, GLuint program, GLenum pname,
                             GLint * params)
{
  if (params != NULL) *params = 0;
  if (glue->glGetProgramiv) glue->glGetProgramiv(program, pname, params);
  else if (glue->glGetObjectParameterivARB) {
    glue->glGetObjectParameterivARB((COIN_GLhandle) program, pname, params);
  }
}

void
cc_glglue_glGetProgramInfoLog(const cc_glglue * glue, GLuint program,
                              GLsizei maxLength, GLsizei * length, char * infoLog)
{
  if (length != NULL) *length = 0;
  if (infoLog != NULL && maxLength > 0) infoLog[0] = '\0';
  if (glue->glGetProgramInfoLog) {
    glue->glGetProgramInfoLog(program, maxLength, length, infoLog);
  }
  else if (glue->glGetInfoLogARB) {
    glue->glGetInfoLogARB((COIN_GLhandle) program, maxLength, length,
                          (COIN_GLchar *) infoLog);
  }
}

void
cc_glglue_glProgramParameteriEXT(const cc_glglue * glue, GLuint program, GLenum pname,
                                 GLint value)
{
  if (glue->glProgramParameteriEXT) {
    glue->glProgramParameteriEXT((COIN_GLhandle) program, pname, value);
  }
}
