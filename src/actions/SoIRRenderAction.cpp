// src/actions/SoIRRenderAction.cpp

#include <Inventor/actions/SoIRRenderAction.h>

#include <Inventor/C/tidbits.h>
#include <Inventor/SbBasic.h>
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoLineWidthElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoEnvironmentElement.h>
#include <Inventor/elements/SoLightAttenuationElement.h>
#include <Inventor/elements/SoLightElement.h>
#include <Inventor/elements/SoLightModelElement.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoNormalBindingElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoOverrideElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include <Inventor/elements/SoShapeStyleElement.h>
#include <Inventor/elements/SoGLShaderProgramElement.h>
#include <Inventor/elements/SoBumpMapCoordinateElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoCacheElement.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoLinePatternElement.h>
#include <Inventor/elements/SoMultiTextureImageElement.h>
#include <Inventor/elements/SoMultiTextureMatrixElement.h>
#include <Inventor/elements/SoCoordinateElement.h>
#include <Inventor/elements/SoNormalElement.h>
#include <Inventor/elements/SoCreaseAngleElement.h>
#include <Inventor/elements/SoComplexityElement.h>
#include <Inventor/elements/SoComplexityTypeElement.h>
#include <Inventor/elements/SoMultiTextureCoordinateElement.h>
#include <Inventor/elements/SoProfileElement.h>
#include <Inventor/elements/SoProfileCoordinateElement.h>
#include <Inventor/elements/SoTextureQualityElement.h>
#include <Inventor/elements/SoSwitchElement.h>
#include <Inventor/elements/SoUnitsElement.h>
#include <Inventor/elements/SoShapeHintsElement.h>
#include <Inventor/elements/SoFocalDistanceElement.h>
#include <Inventor/elements/SoFontNameElement.h>
#include <Inventor/elements/SoFontSizeElement.h>
#include <Inventor/elements/SoGLViewportRegionElement.h>
#include <Inventor/elements/SoGLUpdateAreaElement.h>
#include <Inventor/elements/SoGLRenderPassElement.h>
#include <Inventor/elements/SoGLLightIdElement.h>
#include <Inventor/elements/SoDecimationPercentageElement.h>
#include <Inventor/elements/SoDecimationTypeElement.h>
#include <Inventor/elements/SoTextureOverrideElement.h>
#include <Inventor/elements/SoWindowElement.h>
#include <Inventor/elements/SoPointSizeElement.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoShape.h>

#include "actions/SoSubActionP.h"
#include "elements/SoRenderPlacementElement.h"

#include <cassert>

SO_ACTION_SOURCE(SoIRRenderAction);

class SoIRRenderActionP {
public:
  SoIRRenderActionP() = default;

  SoIRBuffer geometryPool;
  SbList<SoIRRenderAction::PrimitiveCollector *> collectorStack;
};

#define PRIVATE(obj) (obj->pimpl)

void
SoIRRenderAction::initClass(void)
{
  SO_ACTION_INTERNAL_INIT_CLASS(SoIRRenderAction, SoAction);

  if (SoGLShaderProgramElement::getClassTypeId() == SoType::badType()) {
    SoGLShaderProgramElement::initClass();
  }
  if (SoCacheElement::getClassTypeId() == SoType::badType()) {
    SoCacheElement::initClass();
  }
  if (SoRenderPlacementElement::getClassTypeId() == SoType::badType()) {
    SoRenderPlacementElement::initClass();
  }

  SO_ACTION_ADD_METHOD_INTERNAL(SoNode, SoIRRenderAction::renderNode);
  SO_ACTION_ADD_METHOD_INTERNAL(SoShape, SoIRRenderAction::renderShape);

  SO_ENABLE(SoIRRenderAction, SoViewportRegionElement);
  SO_ENABLE(SoIRRenderAction, SoViewVolumeElement);
  SO_ENABLE(SoIRRenderAction, SoViewingMatrixElement);
  SO_ENABLE(SoIRRenderAction, SoProjectionMatrixElement);
  SO_ENABLE(SoIRRenderAction, SoMultiTextureImageElement);
  SO_ENABLE(SoIRRenderAction, SoMultiTextureMatrixElement);
  SO_ENABLE(SoIRRenderAction, SoLinePatternElement);
  SO_ENABLE(SoIRRenderAction, SoOverrideElement);
  SO_ENABLE(SoIRRenderAction, SoModelMatrixElement);
  SO_ENABLE(SoIRRenderAction, SoLazyElement);
  SO_ENABLE(SoIRRenderAction, SoDepthBufferElement);
  SO_ENABLE(SoIRRenderAction, SoDrawStyleElement);
  SO_ENABLE(SoIRRenderAction, SoLineWidthElement);
  SO_ENABLE(SoIRRenderAction, SoPolygonOffsetElement);
  SO_ENABLE(SoIRRenderAction, SoShapeStyleElement);
  SO_ENABLE(SoIRRenderAction, SoLightModelElement);
  SO_ENABLE(SoIRRenderAction, SoLightElement);
  SO_ENABLE(SoIRRenderAction, SoEnvironmentElement);
  SO_ENABLE(SoIRRenderAction, SoLightAttenuationElement);
  SO_ENABLE(SoIRRenderAction, SoMaterialBindingElement);
  SO_ENABLE(SoIRRenderAction, SoNormalBindingElement);
  SO_ENABLE(SoIRRenderAction, SoGLShaderProgramElement);
  SO_ENABLE(SoIRRenderAction, SoCacheElement);
  SO_ENABLE(SoIRRenderAction, SoBumpMapCoordinateElement);
  SO_ENABLE(SoIRRenderAction, SoGLCacheContextElement);
  SO_ENABLE(SoIRRenderAction, SoMultiTextureEnabledElement);

  // Elements needed by generatePrimitives() fallback in SoShape::render()
  SO_ENABLE(SoIRRenderAction, SoCoordinateElement);
  SO_ENABLE(SoIRRenderAction, SoNormalElement);
  SO_ENABLE(SoIRRenderAction, SoCreaseAngleElement);
  SO_ENABLE(SoIRRenderAction, SoComplexityElement);
  SO_ENABLE(SoIRRenderAction, SoComplexityTypeElement);
  SO_ENABLE(SoIRRenderAction, SoMultiTextureCoordinateElement);
  SO_ENABLE(SoIRRenderAction, SoProfileElement);
  SO_ENABLE(SoIRRenderAction, SoProfileCoordinateElement);
  SO_ENABLE(SoIRRenderAction, SoTextureQualityElement);
  SO_ENABLE(SoIRRenderAction, SoSwitchElement);
  SO_ENABLE(SoIRRenderAction, SoUnitsElement);

  // Scene state elements needed by standard nodes during traversal
  SO_ENABLE(SoIRRenderAction, SoShapeHintsElement);
  SO_ENABLE(SoIRRenderAction, SoFocalDistanceElement);
  SO_ENABLE(SoIRRenderAction, SoFontNameElement);
  SO_ENABLE(SoIRRenderAction, SoFontSizeElement);
  SO_ENABLE(SoIRRenderAction, SoPointSizeElement);
  SO_ENABLE(SoIRRenderAction, SoGLViewportRegionElement);
  SO_ENABLE(SoIRRenderAction, SoGLUpdateAreaElement);
  SO_ENABLE(SoIRRenderAction, SoGLRenderPassElement);
  SO_ENABLE(SoIRRenderAction, SoGLLightIdElement);
  SO_ENABLE(SoIRRenderAction, SoDecimationPercentageElement);
  SO_ENABLE(SoIRRenderAction, SoDecimationTypeElement);
  SO_ENABLE(SoIRRenderAction, SoTextureOverrideElement);
  SO_ENABLE(SoIRRenderAction, SoWindowElement);
  SO_ENABLE(SoIRRenderAction, SoRenderPlacementElement);
}

SoIRRenderAction::SoIRRenderAction(const SbViewportRegion & vp)
  : SoAction(), vpRegion(vp), camera(NULL), pimpl(new SoIRRenderActionP)
{
  SO_ACTION_CONSTRUCTOR(SoIRRenderAction);
}

SoIRRenderAction::~SoIRRenderAction()
{
  delete PRIVATE(this);
  PRIVATE(this) = NULL;
}

void
SoIRRenderAction::setViewportRegion(const SbViewportRegion & vp)
{
  this->vpRegion = vp;
}

void
SoIRRenderAction::setCamera(SoCamera * cam)
{
  this->camera = cam;
}

void
SoIRRenderAction::apply(SoNode * root)
{
  this->drawlist.clear();
  this->resetFrameResources();
  inherited::apply(root);
}

void
SoIRRenderAction::storeCommandPath(int commandIndex, const SoPath * path)
{
  (void)commandIndex;
  (void)path;
}

SoPath *
SoIRRenderAction::getCommandPath(int commandIndex) const
{
  (void)commandIndex;
  return nullptr;
}

void
SoIRRenderAction::apply(SoPath * path)
{
  this->drawlist.clear();
  this->resetFrameResources();
  inherited::apply(path);
}

void
SoIRRenderAction::apply(const SoPathList & pathlist, SbBool obeysrules)
{
  this->drawlist.clear();
  this->resetFrameResources();
  inherited::apply(pathlist, obeysrules);
}

void
SoIRRenderAction::traverseAdditionalRoot(SoNode * root)
{
  // Don't clear draw list or reset frame resources — append to existing
  if (!root) return;
  this->state->push();
  SoViewportRegionElement::set(this->state, this->vpRegion);
  this->traverse(root);
  this->state->pop();
}

void
SoIRRenderAction::beginTraversal(SoNode * node)
{
  SoViewportRegionElement::set(this->state, this->vpRegion);
  inherited::beginTraversal(node);
}

void
SoIRRenderAction::endTraversal(SoNode * node)
{
  inherited::endTraversal(node);
}

void
SoIRRenderAction::renderNode(SoAction * a, SoNode * node)
{
  // Special case shader programs so they can populate SoGLShaderProgramElement
  if (node->isOfType(SoShaderProgram::getClassTypeId())) {
    SoIRRenderAction * action = static_cast<SoIRRenderAction *>(a);
    static_cast<SoShaderProgram *>(node)->render(action);
    return;
  }
  node->doAction(a);
}

void
SoIRRenderAction::renderShape(SoAction * a, SoNode * node)
{
  SoIRRenderAction * action = static_cast<SoIRRenderAction *>(a);
  SoShape * shape = static_cast<SoShape *>(node);
  shape->render(action);
}

// Shader programs are handled in renderNode() without explicit registration.

void
SoIRRenderAction::pushPrimitiveCollector(PrimitiveCollector * collector)
{
  assert(collector != NULL);
  PRIVATE(this)->collectorStack.append(collector);
}

void
SoIRRenderAction::popPrimitiveCollector(PrimitiveCollector * collector)
{
  const int count = PRIVATE(this)->collectorStack.getLength();
  assert(count > 0);
  assert(PRIVATE(this)->collectorStack[count - 1] == collector);
  PRIVATE(this)->collectorStack.remove(count - 1);
}

SoIRRenderAction::PrimitiveCollector *
SoIRRenderAction::getActivePrimitiveCollector(void) const
{
  const int count = PRIVATE(this)->collectorStack.getLength();
  if (count == 0) return NULL;
  return PRIVATE(this)->collectorStack[count - 1];
}

void *
SoIRRenderAction::allocateGeometryStorage(size_t bytes, size_t alignment)
{
  return PRIVATE(this)->geometryPool.allocate(bytes, alignment);
}

SoIRBuffer::SavePoint
SoIRRenderAction::saveGeometryPool() const
{
  return PRIVATE(this)->geometryPool.save();
}

void
SoIRRenderAction::rewindGeometryPool(const SoIRBuffer::SavePoint & sp)
{
  PRIVATE(this)->geometryPool.rewindTo(sp);
}

void
SoIRRenderAction::clearGeometryPool()
{
  PRIVATE(this)->geometryPool.clear();
}

void
SoIRRenderAction::resetFrameResources()
{
  PRIVATE(this)->geometryPool.clear();
  PRIVATE(this)->collectorStack.truncate(0);
}
