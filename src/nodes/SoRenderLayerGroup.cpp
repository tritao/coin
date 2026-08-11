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
  \class SoRenderLayerGroup SoRenderLayerGroup.h Inventor/nodes/SoRenderLayerGroup.h
  \brief The SoRenderLayerGroup class renders its children in a selected render layer.

  \ingroup coin_nodes

  This group-type node carries render placement information for its child
  subtree.  It can assign child geometry to a render layer, optionally render
  the subtree with a local pixel viewport, and optionally clear the depth
  buffer inside the effective viewport before traversing the children.

  SoRenderLayerGroup is intentionally limited to placement operations.  It does
  not set camera, projection, material, lighting, culling, depth test, or depth
  write state.  Those states should be expressed with the ordinary scene graph
  nodes and elements used by the child subtree.

  <b>FILE FORMAT/DEFAULTS:</b>
  \code
    RenderLayerGroup {
        renderCaching AUTO
        boundingBoxCaching AUTO
        renderCulling AUTO
        pickCulling AUTO
        layer INHERIT
        viewportOverride FALSE
        viewportPixels 0 0 0 0
        clearDepthBuffer FALSE
    }
  \endcode

  \COIN_CLASS_EXTENSION
*/

#include <Inventor/nodes/SoRenderLayerGroup.h>

#include <cmath>

#include <Inventor/SbViewportRegion.h>
#if COIN_HAVE_LEGACY_GL_RENDERER
#include <Inventor/actions/SoGLRenderAction.h>
#endif
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/elements/SoCacheElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/misc/SoChildList.h>
#include <Inventor/misc/SoState.h>
#if COIN_HAVE_LEGACY_GL_RENDERER
#include <Inventor/system/gl.h>
#endif

#include "elements/SoRenderPlacementElement.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif // HAVE_CONFIG_H
#include "nodes/SoSubNodeP.h"

SO_NODE_SOURCE(SoRenderLayerGroup);

/*!
  \enum SoRenderLayerGroup::Layer
  Enumeration for assigning child geometry to a render layer.
*/

/*!
  \var SoRenderLayerGroup::Layer SoRenderLayerGroup::INHERIT
  Render children in the inherited render layer. This is the default.
*/

/*!
  \var SoRenderLayerGroup::Layer SoRenderLayerGroup::OVERLAY
  Render children in the overlay render layer.
*/

/*!
  \var SoSFEnum SoRenderLayerGroup::layer

  The render layer used for the child subtree. Defaults to INHERIT.
*/

/*!
  \var SoSFBool SoRenderLayerGroup::viewportOverride

  When TRUE, use viewportPixels as the effective viewport while traversing the
  child subtree. When FALSE, the inherited viewport is used. Defaults to FALSE.
*/

/*!
  \var SoSFVec4f SoRenderLayerGroup::viewportPixels

  The local viewport rectangle in pixels, specified as x, y, width, height.
  This field is used only when viewportOverride is TRUE. Defaults to
  [0.0, 0.0, 0.0, 0.0].
*/

/*!
  \var SoSFBool SoRenderLayerGroup::clearDepthBuffer

  When TRUE, clear the depth buffer inside the effective viewport before
  rendering the child subtree. Defaults to FALSE.
*/

namespace {

#if COIN_HAVE_LEGACY_GL_RENDERER
struct ViewportPixels {
  int x;
  int y;
  int width;
  int height;
};

static SbBool
coin_get_viewport_pixels(SoState * state,
                         const SoSFVec4f & field,
                         SbBool overrideViewport,
                         ViewportPixels & pixels)
{
  if (!overrideViewport) {
    const SbViewportRegion & vp = SoViewportRegionElement::get(state);
    const SbVec2s & origin = vp.getViewportOriginPixels();
    const SbVec2s & size = vp.getViewportSizePixels();
    pixels = {origin[0], origin[1], size[0], size[1]};
    return size[0] > 0 && size[1] > 0;
  }

  const SbVec4f & rect = field.getValue();
  pixels = {
    static_cast<int>(std::lround(rect[0])),
    static_cast<int>(std::lround(rect[1])),
    static_cast<int>(std::lround(rect[2])),
    static_cast<int>(std::lround(rect[3]))
  };
  return pixels.width > 0 && pixels.height > 0;
}

static void
coin_set_viewport_element(SoState * state, const ViewportPixels & pixels)
{
  SbViewportRegion vp = SoViewportRegionElement::get(state);
  vp.setViewportPixels(pixels.x, pixels.y, pixels.width, pixels.height);
  SoViewportRegionElement::set(state, vp);
}

class ScopedGLRenderLayerState {
public:
  ScopedGLRenderLayerState()
  {
    glGetIntegerv(GL_VIEWPORT, this->viewport);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &this->depthWriteMask);
    this->scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_SCISSOR_BOX, this->scissorBox);
    glGetDoublev(GL_DEPTH_CLEAR_VALUE, &this->clearDepth);
  }

  ~ScopedGLRenderLayerState()
  {
    glViewport(this->viewport[0], this->viewport[1], this->viewport[2], this->viewport[3]);
    glDepthMask(this->depthWriteMask);
    glScissor(this->scissorBox[0], this->scissorBox[1], this->scissorBox[2], this->scissorBox[3]);
    if (this->scissorEnabled == GL_TRUE) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);
    glClearDepth(this->clearDepth);
  }

private:
  GLint viewport[4] = {0, 0, 0, 0};
  GLboolean depthWriteMask = GL_TRUE;
  GLboolean scissorEnabled = GL_FALSE;
  GLint scissorBox[4] = {0, 0, 0, 0};
  GLdouble clearDepth = 1.0;
};

static void
coin_clear_depth_in_viewport(const ViewportPixels & pixels)
{
  glEnable(GL_SCISSOR_TEST);
  glScissor(pixels.x, pixels.y, pixels.width, pixels.height);
  glDepthMask(GL_TRUE);
  glClearDepth(1.0);
  glClear(GL_DEPTH_BUFFER_BIT);
}

#endif

} // namespace

SoRenderLayerGroup::SoRenderLayerGroup()
{
  SO_NODE_INTERNAL_CONSTRUCTOR(SoRenderLayerGroup);

  SO_NODE_ADD_FIELD(layer, (SoRenderLayerGroup::INHERIT));
  SO_NODE_ADD_FIELD(viewportOverride, (FALSE));
  SO_NODE_ADD_FIELD(viewportPixels, (0.0f, 0.0f, 0.0f, 0.0f));
  SO_NODE_ADD_FIELD(clearDepthBuffer, (FALSE));

  SO_NODE_DEFINE_ENUM_VALUE(Layer, INHERIT);
  SO_NODE_DEFINE_ENUM_VALUE(Layer, FOREGROUND);
  SO_NODE_DEFINE_ENUM_VALUE(Layer, OVERLAY);
  SO_NODE_SET_SF_ENUM_TYPE(layer, Layer);
}

SoRenderLayerGroup::~SoRenderLayerGroup()
{
}

void
SoRenderLayerGroup::initClass(void)
{
  SO_NODE_INTERNAL_INIT_CLASS(SoRenderLayerGroup, SO_FROM_COIN_4_0|SoNode::COIN_4_0);
}

#if COIN_HAVE_LEGACY_GL_RENDERER
void
SoRenderLayerGroup::GLRender(SoGLRenderAction * action)
{
  switch (action->getCurPathCode()) {
  case SoAction::NO_PATH:
  case SoAction::BELOW_PATH:
    this->GLRenderBelowPath(action);
    break;
  case SoAction::OFF_PATH:
    this->GLRenderOffPath(action);
    break;
  case SoAction::IN_PATH:
    this->GLRenderInPath(action);
    break;
  }
}

void
SoRenderLayerGroup::GLRenderBelowPath(SoGLRenderAction * action)
{
  this->GLRenderLayer(action);
}

void
SoRenderLayerGroup::GLRenderInPath(SoGLRenderAction * action)
{
  this->GLRenderLayer(action);
}

void
SoRenderLayerGroup::GLRenderOffPath(SoGLRenderAction *)
{
}

void
SoRenderLayerGroup::GLRenderLayer(SoGLRenderAction * action)
{
  if (!action) return;

  SoState * state = action->getState();
  if (!state) return;

  ViewportPixels pixels;
  if (!coin_get_viewport_pixels(state,
                                this->viewportPixels,
                                this->viewportOverride.getValue(),
                                pixels)) {
    return;
  }

  SoCacheElement::invalidate(state);
  state->push();
  ScopedGLRenderLayerState glstate;
  if (this->viewportOverride.getValue()) {
    coin_set_viewport_element(state, pixels);
    glViewport(pixels.x, pixels.y, pixels.width, pixels.height);
  }
  if (this->clearDepthBuffer.getValue()) {
    coin_clear_depth_in_viewport(pixels);
  }
  this->getChildren()->traverse(action);
  state->pop();
}
#endif

void
SoRenderLayerGroup::doAction(SoAction * action)
{
  if (!action) {
    return;
  }

  if (action->isOfType(SoIRRenderAction::getClassTypeId())) {
    SoIRRenderAction * retained = static_cast<SoIRRenderAction *>(action);
    SoState * state = action->getState();
    if (!state) return;

    const int layerValue = this->layer.getValue();
    int viewportX = 0;
    int viewportY = 0;
    int viewportWidth = 0;
    int viewportHeight = 0;
    if (this->viewportOverride.getValue()) {
      const SbVec4f & rect = this->viewportPixels.getValue();
      viewportX = static_cast<int>(std::lround(rect[0]));
      viewportY = static_cast<int>(std::lround(rect[1]));
      viewportWidth = static_cast<int>(std::lround(rect[2]));
      viewportHeight = static_cast<int>(std::lround(rect[3]));
      if (viewportWidth <= 0 || viewportHeight <= 0) {
        return;
      }
    }

    state->push();
    if (this->viewportOverride.getValue()) {
      SbViewportRegion viewport = SoViewportRegionElement::get(state);
      viewport.setViewportPixels(viewportX, viewportY,
                                 viewportWidth, viewportHeight);
      SoViewportRegionElement::set(state, viewport);
      SoRenderPlacementElement::setViewport(state, viewportX, viewportY,
                                            viewportWidth, viewportHeight);
      SoRenderPlacementElement::setCommandMatricesOverride(state, TRUE);
    }
    if (layerValue == SoRenderLayerGroup::FOREGROUND) {
      SoRenderPlacementElement::setLayer(state, SoRenderPlacementElement::FOREGROUND);
    }
    if (this->clearDepthBuffer.getValue()) {
      retained->requestDepthClear();
    }
    inherited::doAction(action);
    state->pop();
    return;
  }

  inherited::doAction(action);
}
