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

/*!
  \class SoBGFXShaderProgramElement Inventor/elements/SoBGFXShaderProgramElement.h
  \brief The SoBGFXShaderProgramElement class is yet to be documented.

  \ingroup coin_elements

  FIXME: write doc.
*/

#include <Inventor/elements/SoBGFXShaderProgramElement.h>

#include <cassert>

//#include <Inventor/elements/SoGLCacheContextElement.h>
#include "SoBGFXShaderProgram.h"

// *************************************************************************

SO_ELEMENT_SOURCE(SoBGFXShaderProgramElement);

// *************************************************************************

/*!
  \copydetails SoElement::initClass(void)
*/

void
SoBGFXShaderProgramElement::initClass(void)
{
  SO_ELEMENT_INIT_CLASS(SoBGFXShaderProgramElement, inherited);
}

/*!
  Destructor.
*/

SoBGFXShaderProgramElement::~SoBGFXShaderProgramElement()
{
  this->shaderProgram = NULL;
}

void
SoBGFXShaderProgramElement::init(SoState *state)
{
  inherited::init(state);
  this->shaderProgram = NULL;
}

void
SoBGFXShaderProgramElement::set(SoState* const state, SoNode *const node,
                              SoBGFXShaderProgram* program)
{
  SoBGFXShaderProgramElement* element =
    (SoBGFXShaderProgramElement*)inherited::getElement(state,classStackIndex, node);
  element->shaderProgram = program;
}

SoBGFXShaderProgram *
SoBGFXShaderProgramElement::get(SoState *state)
{
  const SoElement *element = getConstElement(state, classStackIndex);
  assert(element);
  return ((const SoBGFXShaderProgramElement *)element)->shaderProgram;
}

SbBool
SoBGFXShaderProgramElement::matches(const SoElement * element) const
{
  SoBGFXShaderProgramElement * elem = (SoBGFXShaderProgramElement*) element;
  return (this->shaderProgram == elem->shaderProgram);
}

SoElement *
SoBGFXShaderProgramElement::copyMatchInfo(void) const
{
  SoBGFXShaderProgramElement * elem = 
    (SoBGFXShaderProgramElement*) inherited::copyMatchInfo();
  elem->shaderProgram = this->shaderProgram;
  return elem;
}
