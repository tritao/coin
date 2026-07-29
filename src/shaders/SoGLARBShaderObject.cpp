/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\**************************************************************************/

#include "SoGLARBShaderObject.h"

#include <cstring>

#include <Inventor/errors/SoDebugError.h>
#include "SoGLARBShaderParameter.h"

// *************************************************************************

SoGLARBShaderObject::SoGLARBShaderObject(const uint32_t cachecontext)
  : SoGLShaderObject(cachecontext)
{
  this->arbProgramID = 0;
}

SoGLARBShaderObject::~SoGLARBShaderObject()
{
}

SbBool
SoGLARBShaderObject::isLoaded(void) const
{
#if !defined(COIN_GL_COMPATIBILITY)
  return FALSE;
#else
  return cc_glglue_glIsProgram(this->glctx, this->arbProgramID);
#endif
}

void
SoGLARBShaderObject::load(const char * srcStr)
{
#if !defined(COIN_GL_COMPATIBILITY)
  (void)srcStr;
  return;
#else
  const size_t len = strlen(srcStr);
  
  this->target = this->getShaderType() == VERTEX 
    ? GL_VERTEX_PROGRAM_ARB : GL_FRAGMENT_PROGRAM_ARB;

  this->unload();

  if (len == 0) return;

  glEnable(this->target);
  cc_glglue_glGenPrograms(this->glctx, 1, &this->arbProgramID);
  cc_glglue_glBindProgram(this->glctx, this->target, this->arbProgramID);
  cc_glglue_glProgramString(this->glctx, this->target, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)len, srcStr);

  if (glGetError() == GL_INVALID_OPERATION) {
    GLint errorPos;
    const GLubyte *errorString;

    glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &errorPos);
    errorString = glGetString(GL_PROGRAM_ERROR_STRING_ARB);
    SoDebugError::post("SoGLARBShaderObject::load",
                       "Error at position: %d (%s)",
                       errorPos, errorString);
  }

  glDisable(this->target);
#endif
}

void
SoGLARBShaderObject::unload(void)
{
#if !defined(COIN_GL_COMPATIBILITY)
  return;
#else
  if (cc_glglue_glIsProgram(this->glctx, this->arbProgramID)) {
    // FIXME: make sure this is only called when in the correct, valid
    // GL context. 20050120 mortene.
    cc_glglue_glDeletePrograms(this->glctx, 1, &this->arbProgramID);
    this->arbProgramID = 0;
  }
#endif
}

SoShader::Type
SoGLARBShaderObject::shaderType(void) const
{
  return SoShader::ARB_SHADER;
}

SoGLShaderParameter *
SoGLARBShaderObject::getNewParameter(void) const
{
  return new SoGLARBShaderParameter();
}

void
SoGLARBShaderObject::enable(void)
{
#if !defined(COIN_GL_COMPATIBILITY)
  return;
#else
  if (this->isActive()) {
    cc_glglue_glBindProgram(this->glctx, this->target, this->arbProgramID);
    glEnable(this->target);
  }
#endif
}

void
SoGLARBShaderObject::disable(void)
{
#if !defined(COIN_GL_COMPATIBILITY)
  return;
#else
  if (this->isActive()) glDisable(this->target);
#endif
}
