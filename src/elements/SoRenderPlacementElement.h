#ifndef COIN_SORENDERPLACEMENTELEMENT_H
#define COIN_SORENDERPLACEMENTELEMENT_H

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

#ifndef COIN_INTERNAL
#error this is a private header file
#endif // !COIN_INTERNAL

#include <Inventor/elements/SoSubElement.h>

class SoRenderPlacementElement : public SoElement {
  typedef SoElement inherited;

  SO_ELEMENT_HEADER(SoRenderPlacementElement);

public:
  enum Layer {
    MAIN = 0,
    FOREGROUND = 1
  };

  static void initClass(void);

protected:
  virtual ~SoRenderPlacementElement();

public:
  virtual void init(SoState * state);
  virtual void push(SoState * state);
  virtual void pop(SoState * state, const SoElement * prevTopElement);

  virtual SbBool matches(const SoElement * element) const;
  virtual SoElement * copyMatchInfo(void) const;

  static void setLayer(SoState * state, Layer layer);
  static void setViewport(SoState * state,
                          int x, int y, int width, int height);
  static void setClearDepth(SoState * state, SbBool clearDepth);

  static Layer getLayer(SoState * state);
  static SbBool getViewport(SoState * state,
                            int & x, int & y, int & width, int & height);
  static SbBool getClearDepth(SoState * state);

private:
  Layer layer;
  SbBool viewportOverride;
  int viewportX;
  int viewportY;
  int viewportWidth;
  int viewportHeight;
  SbBool clearDepth;
};

#endif // !COIN_SORENDERPLACEMENTELEMENT_H
