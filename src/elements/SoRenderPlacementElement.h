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

/*!
  \class SoRenderPlacementElement
  \brief Private traversal state for retained-render placement directives.

  This element is enabled by SoIRRenderAction and carries action-local
  placement information through the normal Inventor state stack. It is not
  OpenGL state and does not issue backend commands itself. The default state
  is MAIN, with no viewport override. Values are
  inherited by push() and restored by the usual element-stack mechanism.

  Viewport values are read while the action converts traversal state into
  retained commands or depth-clear barriers. The AfterMain render stage is
  controlled separately by SoIRRenderStageScope; it is not represented by
  Layer.
*/
class SoRenderPlacementElement : public SoElement {
  typedef SoElement inherited;

  SO_ELEMENT_HEADER(SoRenderPlacementElement);

public:
  //! Logical placement hint. MAIN is the default; FOREGROUND is reserved for
  //! traversal code that explicitly consumes this element.
  enum Layer {
    MAIN = 0,
    FOREGROUND = 1
  };

  static void initClass(void);

protected:
  virtual ~SoRenderPlacementElement();

public:
  virtual void init(SoState * state) override;
  virtual void push(SoState * state) override;
  virtual void pop(SoState * state, const SoElement * prevTopElement) override;

  virtual SbBool matches(const SoElement * element) const override;
  virtual SoElement * copyMatchInfo(void) const override;

  //! Set the logical placement hint for subsequent traversal.
  static void setLayer(SoState * state, Layer layer);

  //! Request a viewport override in framebuffer pixel coordinates.
  static void setViewport(SoState * state,
                          int x, int y, int width, int height);

  //! Select command-local view/projection matrices for retained rendering.
  static void setCommandMatricesOverride(SoState * state, SbBool enabled);

  //! Return the current logical placement hint.
  static Layer getLayer(SoState * state);

  //! Return whether a viewport override is active and, if so, its rectangle.
  static SbBool getViewport(SoState * state,
                            int & x, int & y, int & width, int & height);

  //! Return whether command-local matrices are selected.
  static SbBool getCommandMatricesOverride(SoState * state);

private:
  Layer layer;
  SbBool viewportOverride;
  int viewportX;
  int viewportY;
  int viewportWidth;
  int viewportHeight;
  SbBool commandMatricesOverride;
};

#endif // !COIN_SORENDERPLACEMENTELEMENT_H
