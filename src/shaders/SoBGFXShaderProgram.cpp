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

#include "shaders/SoBGFXShaderProgram.h"

#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/misc/SoContextHandler.h>

#include "Inventor/C/glue/gl.h"
#include "shaders/SoBGFXShaderObject.h"
#include <Inventor/errors/SoDebugError.h>
#include "glue/glp.h"

SoBGFXShaderProgram::SoBGFXShaderProgram(void)
{
  //SoContextHandler::addContextDestructionCallback(context_destruction_cb, this);
}

SoBGFXShaderProgram::~SoBGFXShaderProgram()
{
  //SoContextHandler::removeContextDestructionCallback(context_destruction_cb, this);
  bgfx::destroy(this->programHandle);
}

SoBGFXShaderProgram * SoBGFXShaderProgram::create(bgfx::ShaderHandle vertex, bgfx::ShaderHandle fragment)
{
  bgfx::ProgramHandle programHandle = bgfx::createProgram(vertex, fragment);
  if (!bgfx::isValid(programHandle)) {
    return NULL;
  }

  SoBGFXShaderProgram * program = new SoBGFXShaderProgram();
  program->setVertexShader(vertex);
  program->setFragmentShader(fragment);
  program->setProgramHandle(programHandle);

  return program;
}

SoBGFXShaderProgram * SoBGFXShaderProgram::create(bgfx::ShaderHandle compute)
{
  bgfx::ProgramHandle programHandle = bgfx::createProgram(compute);
  if (!bgfx::isValid(programHandle)) {
    return NULL;
  }

  SoBGFXShaderProgram * program = new SoBGFXShaderProgram();
  program->setComputeShader(compute);

  return program;
}

SoBGFXShaderProgram *
SoBGFXShaderProgram::create(const SbName& vertex, const SbName& fragment)
{
    bgfx::EmbeddedShader vertexEmbeddedShader;
    if (!SoShader::getBGFXEmbeddedShader(vertex.getString(), vertexEmbeddedShader)) {
      SoDebugError::post("SoShape::setupShaders", "Missing vertex shader");
      return NULL;
    }
    bgfx::ShaderHandle vertexShaderHandle;
    SoBGFXShaderObject::create(vertexEmbeddedShader, vertexShaderHandle);

    bgfx::EmbeddedShader fragmentEmbeddedShader;
    if (!SoShader::getBGFXEmbeddedShader(fragment.getString(), fragmentEmbeddedShader)) {
      SoDebugError::post("SoShape::setupShaders", "Missing fragment shader");
      return NULL;
    }
    bgfx::ShaderHandle fragmentShaderHandle;
    SoBGFXShaderObject::create(fragmentEmbeddedShader, fragmentShaderHandle);

    SoBGFXShaderProgram * shaderProgram = SoBGFXShaderProgram::create(vertexShaderHandle, fragmentShaderHandle);
    if (shaderProgram == NULL) {
      SoDebugError::post("SoShape::setupShaders", "Missing shader program for BGFX renderer.");
      return NULL;
    }

    return shaderProgram;
}

bgfx::ProgramHandle SoBGFXShaderProgram::getProgramHandle() const
{
  return this->programHandle;
}

void
SoBGFXShaderProgram::setVertexShader(bgfx::ShaderHandle shader)
{
  this->vertexShader = shader;
}

void
SoBGFXShaderProgram::setFragmentShader(bgfx::ShaderHandle shader)
{
  this->fragmentShader = shader;
}

void
SoBGFXShaderProgram::setComputeShader(bgfx::ShaderHandle shader)
{
  this->computeShader = shader;
}

void
SoBGFXShaderProgram::setProgramHandle(bgfx::ProgramHandle program)
{
  this->programHandle = program;
}

#if 0
void
SoBGFXShaderProgram::context_destruction_cb(uint32_t cachecontext, void * userdata)
{
  SoGLSLShaderProgram * thisp = (SoGLSLShaderProgram*) userdata;

  COIN_GLhandle glhandle = 0;
  if (thisp->programHandles.get(cachecontext, glhandle)) {
    // just delete immediately. The context is current
    const cc_glglue * glue = cc_glglue_instance(cachecontext);
    glue->glDeleteObjectARB(glhandle);
    thisp->programHandles.erase(cachecontext);
  }
}

void
SoBGFXShaderProgram::really_delete_object(void * closure, uint32_t contextid)
{
  uintptr_t tmp = (uintptr_t) closure;

  COIN_GLhandle glhandle = (COIN_GLhandle) tmp;

  const cc_glglue * glue = cc_glglue_instance(contextid);
  glue->glDeleteObjectARB(glhandle);
}
#endif

void
SoBGFXShaderProgram::updateCoinParameter(SoState * state, const SbName & name, const int value)
{
#if 0
  const int n = this->shaderObjects.getLength();
  for (int i = 0; i < n; i++) {
    this->shaderObjects[i]->updateCoinParameter(state, name, NULL, value);
  }
#endif
}

void
SoBGFXShaderProgram::addProgramParameter(int mode, int value)
{
#if 0
  this->programParameters.append(mode);
  this->programParameters.append(value);
#endif
}

#if 0
void
SoBGFXShaderProgram::removeProgramParameters(void)
{
#if 0
  this->programParameters.truncate(0);
#endif
}
#endif
