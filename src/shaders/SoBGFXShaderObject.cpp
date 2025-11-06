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

#include "SoBGFXShaderObject.h"
#include "coindefs.h"

#include <cassert>


static uint32_t shaderid = 0;

// *************************************************************************

SoBGFXShaderObject::SoBGFXShaderObject(const uint32_t cachecontext)
{
  this->isActiveFlag = TRUE;
  this->shadertype = VERTEX;
  this->paramsdirty = TRUE;
  this->cachecontext = cachecontext;
  this->id = ++shaderid;
}

uint32_t
SoBGFXShaderObject::getCacheContext(void) const
{
  return this->cachecontext;
}

SoShader::Type
SoBGFXShaderObject::shaderType(void) const
{
  return SoShader::BGFX_SHADER;
}

// SoGLShaderParameter *
// SoBGFXShaderObject::getNewParameter(void) const
// {
//   return new SoGLCgShaderParameter();
// }

SbBool
SoBGFXShaderObject::isLoaded(void) const
{
  return true;
}

bool SoBGFXShaderObject::create(const bgfx::EmbeddedShader& shader, bgfx::ShaderHandle& handle)
{
  bgfx::RendererType::Enum renderer_type = bgfx::getRendererType();
  for (const bgfx::EmbeddedShader::Data* esd = shader.data; bgfx::RendererType::Count != esd->type; ++esd) {
    if (esd->type == renderer_type && esd->size > 1)
    {
      handle = createShader(bgfx::makeRef(esd->data, esd->size) );
      bgfx::setName(handle, shader.name);
      return true;
    }
  }
  return false;
}

void
SoBGFXShaderObject::load(const bgfx::EmbeddedShader& shader)
{
  if (!create(shader, this->handle)) {
    SoDebugError::post("SoBGFXShaderObject::load", "Failure to load embedded BGFX shader.");
  }
}

void
SoBGFXShaderObject::unload(void)
{
  // if (glue_cgIsProgram(this->cgProgram)) {
  //   glue_cgDestroyProgram(this->cgProgram);
  //   this->cgProgram = NULL;
  // }
}


void
SoBGFXShaderObject::setShaderType(const ShaderType type)
{
  if (this->shadertype != type) {
    this->unload();
    this->shadertype = type;
  }
}

SoBGFXShaderObject::ShaderType
SoBGFXShaderObject::getShaderType(void) const
{
  return this->shadertype;
}

void SoBGFXShaderObject::setIsActive(SbBool flag)
{
  this->isActiveFlag = flag;
}

SbBool
SoBGFXShaderObject::isActive(void) const
{
  return (!this->isLoaded()) ? FALSE : this->isActiveFlag;
}

void
SoBGFXShaderObject::setParametersDirty(SbBool flag)
{
  this->paramsdirty = flag;
}

SbBool
SoBGFXShaderObject::getParametersDirty(void) const
{
  return this->paramsdirty;
}

void
SoBGFXShaderObject::updateCoinParameter(SoState * COIN_UNUSED_ARG(state), const SbName & COIN_UNUSED_ARG(name), SoShaderParameter * COIN_UNUSED_ARG(param), const int COIN_UNUSED_ARG(val))
{
}

uint32_t 
SoBGFXShaderObject::getShaderObjectId(void) const
{
  return this->id;
}
