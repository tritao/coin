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

#include "SoRenderPlacementElement.h"

#include <cassert>

SO_ELEMENT_SOURCE(SoRenderPlacementElement);

void
SoRenderPlacementElement::initClass(void)
{
  SO_ELEMENT_INIT_CLASS(SoRenderPlacementElement, inherited);
}

SoRenderPlacementElement::~SoRenderPlacementElement()
{
}

void
SoRenderPlacementElement::init(SoState * state)
{
  inherited::init(state);
  this->layer = MAIN;
  this->viewportOverride = FALSE;
  this->viewportX = 0;
  this->viewportY = 0;
  this->viewportWidth = 0;
  this->viewportHeight = 0;
  this->commandMatricesOverride = FALSE;
}

void
SoRenderPlacementElement::push(SoState * state)
{
  inherited::push(state);
  const SoRenderPlacementElement * prev =
    static_cast<const SoRenderPlacementElement *>(this->getNextInStack());
  this->layer = prev->layer;
  this->viewportOverride = prev->viewportOverride;
  this->viewportX = prev->viewportX;
  this->viewportY = prev->viewportY;
  this->viewportWidth = prev->viewportWidth;
  this->viewportHeight = prev->viewportHeight;
  this->commandMatricesOverride = prev->commandMatricesOverride;
}

void
SoRenderPlacementElement::pop(SoState * state,
                              const SoElement * prevTopElement)
{
  inherited::pop(state, prevTopElement);

  (void) prevTopElement;
}

SbBool
SoRenderPlacementElement::matches(const SoElement * /* element */) const
{
  assert(0 && "should never be called.");
  return TRUE;
}

SoElement *
SoRenderPlacementElement::copyMatchInfo(void) const
{
  assert(0 && "should never be called.");
  return NULL;
}

void
SoRenderPlacementElement::setLayer(SoState * state, Layer layer)
{
  SoRenderPlacementElement * elem =
    static_cast<SoRenderPlacementElement *>
    (SoElement::getElement(state, classStackIndex));
  if (elem) {
    elem->layer = layer;
  }
}

void
SoRenderPlacementElement::setViewport(SoState * state,
                                      int x, int y, int width, int height)
{
  SoRenderPlacementElement * elem =
    static_cast<SoRenderPlacementElement *>
    (SoElement::getElement(state, classStackIndex));
  if (elem) {
    elem->viewportOverride = TRUE;
    elem->viewportX = x;
    elem->viewportY = y;
    elem->viewportWidth = width;
    elem->viewportHeight = height;
  }
}

void
SoRenderPlacementElement::setCommandMatricesOverride(SoState * state,
                                                      SbBool enabled)
{
  SoRenderPlacementElement * elem =
    static_cast<SoRenderPlacementElement *>(
      SoElement::getElement(state, classStackIndex));
  if (elem) {
    elem->commandMatricesOverride = enabled;
  }
}

SoRenderPlacementElement::Layer
SoRenderPlacementElement::getLayer(SoState * state)
{
  const SoRenderPlacementElement * elem =
    static_cast<const SoRenderPlacementElement *>
    (SoElement::getConstElement(state, classStackIndex));
  return elem->layer;
}

SbBool
SoRenderPlacementElement::getViewport(SoState * state,
                                      int & x, int & y,
                                      int & width, int & height)
{
  const SoRenderPlacementElement * elem =
    static_cast<const SoRenderPlacementElement *>
    (SoElement::getConstElement(state, classStackIndex));
  x = elem->viewportX;
  y = elem->viewportY;
  width = elem->viewportWidth;
  height = elem->viewportHeight;
  return elem->viewportOverride;
}

SbBool
SoRenderPlacementElement::getCommandMatricesOverride(SoState * state)
{
  const SoRenderPlacementElement * elem =
    static_cast<const SoRenderPlacementElement *>(
      SoElement::getConstElement(state, classStackIndex));
  return elem ? elem->commandMatricesOverride : FALSE;
}
