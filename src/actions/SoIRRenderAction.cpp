// src/actions/SoIRRenderAction.cpp

#include <Inventor/actions/SoIRRenderAction.h>

#include <Inventor/SoPath.h>
#include <Inventor/SoDB.h>
#include <Inventor/C/tidbits.h>
#include <Inventor/SbBasic.h>
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoLineWidthElement.h>
#include <Inventor/elements/SoLinePatternElement.h>
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
#include <Inventor/elements/SoBumpMapCoordinateElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoCacheElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoDevicePixelRatioElement.h>
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
#include <Inventor/elements/SoTextureUnitElement.h>
#include <Inventor/elements/SoSwitchElement.h>
#include <Inventor/elements/SoUnitsElement.h>
#include <Inventor/elements/SoShapeHintsElement.h>
#include <Inventor/elements/SoFocalDistanceElement.h>
#include <Inventor/elements/SoFontNameElement.h>
#include <Inventor/elements/SoFontSizeElement.h>
#include <Inventor/elements/SoDecimationPercentageElement.h>
#include <Inventor/elements/SoDecimationTypeElement.h>
#include <Inventor/elements/SoTextureOverrideElement.h>
#include <Inventor/elements/SoPointSizeElement.h>
#include <Inventor/elements/SoPickStyleElement.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/lists/SoPathList.h>

#include "actions/SoSubActionP.h"
#include "elements/SoRenderPlacementElement.h"
#include "rendering/SoRenderIRP.h"

#include <cassert>
#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

SO_ACTION_SOURCE(SoIRRenderAction);

class SoIRRenderActionP {
public:
  struct TextureStorage {
    const unsigned char * source = nullptr;
    size_t bytes = 0;
    int width = 0;
    int height = 0;
    int numComponents = 0;
    const unsigned char * copy = nullptr;
    bool hasTransparency = false;
  };

  SoIRRenderActionP() = default;

  SoIRBuffer geometryPool;
  std::vector<TextureStorage> textureStorage;
  SbList<SoIRRenderAction::PrimitiveCollector *> collectorStack;
  std::vector<SoNode *> instancePathNodes;
  std::vector<int> instancePathIndices;
  SoInstanceId currentInstanceId = 0;
  SoInstanceId nextInstanceId = 1;
  SoRenderStage renderStage = SoRenderStage::Main;
  SoIRRenderContext renderContextOverride;
  bool hasRenderContextOverride = false;
};

#define PRIVATE(obj) (obj->pimpl)

void
SoIRRenderAction::initClass(void)
{
  SO_ACTION_INTERNAL_INIT_CLASS(SoIRRenderAction, SoAction);

  if (SoCacheElement::getClassTypeId() == SoType::badType()) {
    SoCacheElement::initClass();
  }
  SO_ACTION_ADD_METHOD_INTERNAL(SoNode, SoNode::IRRenderS);
  if (SoRenderPlacementElement::getClassTypeId() == SoType::badType()) {
    SoRenderPlacementElement::initClass();
  }

  SO_ENABLE(SoIRRenderAction, SoViewportRegionElement);
  SO_ENABLE(SoIRRenderAction, SoRenderPlacementElement);
  SO_ENABLE(SoIRRenderAction, SoDevicePixelRatioElement);
  SO_ENABLE(SoIRRenderAction, SoViewVolumeElement);
  SO_ENABLE(SoIRRenderAction, SoViewingMatrixElement);
  SO_ENABLE(SoIRRenderAction, SoProjectionMatrixElement);
  SO_ENABLE(SoIRRenderAction, SoMultiTextureImageElement);
  SO_ENABLE(SoIRRenderAction, SoMultiTextureMatrixElement);
  SO_ENABLE(SoIRRenderAction, SoOverrideElement);
  SO_ENABLE(SoIRRenderAction, SoModelMatrixElement);
  SO_ENABLE(SoIRRenderAction, SoLazyElement);
  SO_ENABLE(SoIRRenderAction, SoDepthBufferElement);
  SO_ENABLE(SoIRRenderAction, SoDrawStyleElement);
  SO_ENABLE(SoIRRenderAction, SoLineWidthElement);
  SO_ENABLE(SoIRRenderAction, SoLinePatternElement);
  SO_ENABLE(SoIRRenderAction, SoPolygonOffsetElement);
  SO_ENABLE(SoIRRenderAction, SoShapeStyleElement);
  SO_ENABLE(SoIRRenderAction, SoLightModelElement);
  SO_ENABLE(SoIRRenderAction, SoLightElement);
  SO_ENABLE(SoIRRenderAction, SoEnvironmentElement);
  SO_ENABLE(SoIRRenderAction, SoLightAttenuationElement);
  SO_ENABLE(SoIRRenderAction, SoMaterialBindingElement);
  SO_ENABLE(SoIRRenderAction, SoNormalBindingElement);
  SO_ENABLE(SoIRRenderAction, SoCacheElement);
  SO_ENABLE(SoIRRenderAction, SoBumpMapCoordinateElement);
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
  SO_ENABLE(SoIRRenderAction, SoTextureUnitElement);
  SO_ENABLE(SoIRRenderAction, SoSwitchElement);
  SO_ENABLE(SoIRRenderAction, SoUnitsElement);

  // Scene state elements needed by standard nodes during traversal
  SO_ENABLE(SoIRRenderAction, SoShapeHintsElement);
  SO_ENABLE(SoIRRenderAction, SoFocalDistanceElement);
  SO_ENABLE(SoIRRenderAction, SoFontNameElement);
  SO_ENABLE(SoIRRenderAction, SoFontSizeElement);
  SO_ENABLE(SoIRRenderAction, SoPointSizeElement);
  SO_ENABLE(SoIRRenderAction, SoPickStyleElement);
  SO_ENABLE(SoIRRenderAction, SoDecimationPercentageElement);
  SO_ENABLE(SoIRRenderAction, SoDecimationTypeElement);
  SO_ENABLE(SoIRRenderAction, SoTextureOverrideElement);
}

SoIRRenderAction::SoIRRenderAction(const SbViewportRegion & vp)
  : SoAction(), vpRegion(vp), pimpl(new SoIRRenderActionP)
{
  SO_ACTION_CONSTRUCTOR(SoIRRenderAction);
}

SoIRRenderAction::~SoIRRenderAction()
{
  this->clearCommandPaths();
  delete PRIVATE(this);
  PRIVATE(this) = NULL;
}

void
SoIRRenderAction::setViewportRegion(const SbViewportRegion & vp)
{
  this->vpRegion = vp;
}

void
SoIRRenderAction::beginFrame()
{
  this->drawlist.clear();
  this->clearCommandPaths();
  this->resetFrameResources();
  this->unsupportedRendering = false;
  this->unsupportedNode = nullptr;
  this->unsupportedReason = nullptr;
}

void
SoIRRenderAction::addCommand(const SoRenderCommand & command)
{
  SoRenderCommand copy = command;
  this->addCommand(std::move(copy));
}

void
SoIRRenderAction::addCommand(SoRenderCommand && command)
{
  SoRenderCommand retained = std::move(command);
  const SoPath * currentPath = this->getCurPath();
  SoNode * tail = currentPath ? currentPath->getTail() : nullptr;
  if (retained.nodeId == 0 && tail) {
    retained.nodeId = static_cast<SoNodeId>(tail->getNodeId());
  }
  if (retained.instanceId == 0 && currentPath) {
    const int length = currentPath->getLength();
    bool samePath = length == static_cast<int>(PRIVATE(this)->instancePathNodes.size());
    for (int i = 0; samePath && i < length; ++i) {
      samePath = currentPath->getNode(i) == PRIVATE(this)->instancePathNodes[i] &&
        currentPath->getIndex(i) == PRIVATE(this)->instancePathIndices[i];
    }
    if (!samePath) {
      PRIVATE(this)->instancePathNodes.resize(length);
      PRIVATE(this)->instancePathIndices.resize(length);
      for (int i = 0; i < length; ++i) {
        PRIVATE(this)->instancePathNodes[i] = currentPath->getNode(i);
        PRIVATE(this)->instancePathIndices[i] = currentPath->getIndex(i);
      }
      PRIVATE(this)->currentInstanceId = PRIVATE(this)->nextInstanceId++;
    }
    retained.instanceId = PRIVATE(this)->currentInstanceId;
  }

  if (!retained.geometry.hasBounds && retained.geometry.positions &&
      retained.geometry.vertexCount > 0) {
    const size_t stride = retained.geometry.vertexStride
      ? retained.geometry.vertexStride : sizeof(float) * 3;
    const char * position = reinterpret_cast<const char *>(
      retained.geometry.positions);
    SbVec3f minimum(std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max());
    SbVec3f maximum(-std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max());
    for (uint32_t i = 0; i < retained.geometry.vertexCount; ++i) {
      const float * vertex = reinterpret_cast<const float *>(
        position + static_cast<size_t>(i) * stride);
      for (int axis = 0; axis < 3; ++axis) {
        minimum[axis] = std::min(minimum[axis], vertex[axis]);
        maximum[axis] = std::max(maximum[axis], vertex[axis]);
      }
    }
    retained.geometry.boundsCenter.setValue(
      (minimum[0] + maximum[0]) * 0.5f,
      (minimum[1] + maximum[1]) * 0.5f,
      (minimum[2] + maximum[2]) * 0.5f);
    retained.geometry.hasBounds = TRUE;
  }
  SoState * state = this->getState();
  const SoIRRenderContext * context = this->getRenderContextOverride();
  if (context && context->hasLighting) {
    retained.lightingHandle = SoRenderIR::fillLightingFromState(
      state, this->drawlist, context->lighting);
  }
  else if (retained.lightingHandle == 0 && state) {
    retained.lightingHandle = SoRenderIR::fillLightingFromState(
      state, this->drawlist);
  }

  const int commandIndex = this->drawlist.getNumCommands();
  this->drawlist.addCommand(std::move(retained));

  SoPath * retainedPath = currentPath ? currentPath->copy() : NULL;
  if (retainedPath) retainedPath->ref();
  if (static_cast<size_t>(commandIndex) >= this->commandPaths.size()) {
    this->commandPaths.resize(static_cast<size_t>(commandIndex) + 1, NULL);
  }
  this->commandPaths[static_cast<size_t>(commandIndex)] = retainedPath;
}

void
SoIRRenderAction::markUnsupported(const SoNode * node, const char * reason)
{
  if (this->unsupportedRendering) return;
  this->unsupportedRendering = true;
  this->unsupportedNode = node;
  this->unsupportedReason = reason ? reason : "unsupported retained rendering semantics";
}

const SoPath *
SoIRRenderAction::getCommandPath(int commandIndex) const
{
  if (commandIndex < 0 ||
      static_cast<size_t>(commandIndex) >= this->commandPaths.size()) {
    return NULL;
  }
  return this->commandPaths[static_cast<size_t>(commandIndex)];
}

void
SoIRRenderAction::apply(SoNode * root)
{
  this->beginFrame();
  inherited::apply(root);
}

void
SoIRRenderAction::apply(SoPath * path)
{
  this->beginFrame();
  inherited::apply(path);
}

void
SoIRRenderAction::apply(const SoPathList & pathlist, SbBool obeysrules)
{
  this->beginFrame();
  inherited::apply(pathlist, obeysrules);
}

void
SoIRRenderAction::initializeCameraState(CameraPolicy policy)
{
  if (policy != CameraPolicy::USE_CONFIGURED_CAMERA || !this->camera) {
    return;
  }

  SbViewportRegion cameraViewport = this->vpRegion;
  const SbViewVolume viewVolume =
    this->camera->getViewVolume(this->vpRegion, cameraViewport);
  SbMatrix viewingMatrix;
  SbMatrix projectionMatrix;
  viewVolume.getMatrices(viewingMatrix, projectionMatrix);
  SoViewportRegionElement::set(this->state, cameraViewport);
  SoViewVolumeElement::set(this->state, this->camera, viewVolume);
  SoViewingMatrixElement::set(this->state, this->camera, viewingMatrix);
  SoProjectionMatrixElement::set(this->state, this->camera, projectionMatrix);
}

void
SoIRRenderAction::traverseAdditionalRoot(SoNode * root, CameraPolicy policy)
{
  if (!root) return;
  this->traversalMethods->setUp();
  this->state->push();
  SoViewportRegionElement::set(this->state, this->vpRegion);
  SoDevicePixelRatioElement::set(this->state, this->devicePixelRatio);
  this->initializeCameraState(policy);
  SoRenderIR::setCommandMatricesOverride(
    this->state, policy == CameraPolicy::CAMERA_IN_ROOT);
  this->switchToNodeTraversal(root);
  this->state->pop();
}

void
SoIRRenderAction::traverseAdditionalPath(SoPath * path)
{
  this->traverseAdditionalPathInternal(path, nullptr);
}

void
SoIRRenderAction::traverseAdditionalPath(SoPath * path,
                                         const SoIRRenderContext & context)
{
  this->traverseAdditionalPathInternal(path, &context);
}

void
SoIRRenderAction::traverseAdditionalPathInternal(
  SoPath * path, const SoIRRenderContext * context)
{
  if (!path) return;

  this->traversalMethods->setUp();
  const bool previousHasContext = PRIVATE(this)->hasRenderContextOverride;
  const SoIRRenderContext previousContext = PRIVATE(this)->renderContextOverride;
  PRIVATE(this)->hasRenderContextOverride = context != nullptr;
  if (context) {
    PRIVATE(this)->renderContextOverride = *context;
  }
  this->state->push();
  if (context) {
    // The path traversal reconstructs model state through its ancestors.
    context->applyToState(this->state, FALSE);
    SoRenderIR::setCommandMatricesOverride(this->state, TRUE);
  }
  else {
    SoViewportRegionElement::set(this->state, this->vpRegion);
    SoDevicePixelRatioElement::set(this->state, this->devicePixelRatio);
    this->initializeCameraState(this->cameraPolicy);
    SoRenderIR::setCommandMatricesOverride(
      this->state, this->cameraPolicy == CameraPolicy::CAMERA_IN_ROOT);
  }
  this->switchToPathTraversal(path);
  this->state->pop();
  PRIVATE(this)->hasRenderContextOverride = previousHasContext;
  if (previousHasContext) {
    PRIVATE(this)->renderContextOverride = previousContext;
  }
}

const SoIRRenderContext *
SoIRRenderAction::getRenderContextOverride() const
{
  return PRIVATE(this)->hasRenderContextOverride
    ? &PRIVATE(this)->renderContextOverride : nullptr;
}

void
SoIRRenderAction::beginTraversal(SoNode * node)
{
  SoViewportRegionElement::set(this->state, this->vpRegion);
  SoDevicePixelRatioElement::set(this->state, this->devicePixelRatio);
  this->initializeCameraState(this->cameraPolicy);
  SoRenderIR::setCommandMatricesOverride(
    this->state, this->cameraPolicy == CameraPolicy::CAMERA_IN_ROOT);
  inherited::beginTraversal(node);
}

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

const unsigned char *
SoIRRenderAction::allocateTextureStorage(const unsigned char * source,
                                         size_t bytes,
                                         int width,
                                         int height,
                                         int numComponents,
                                         bool & hasTransparency)
{
  assert(source != NULL);
  for (std::vector<SoIRRenderActionP::TextureStorage>::const_iterator it =
         PRIVATE(this)->textureStorage.begin();
       it != PRIVATE(this)->textureStorage.end(); ++it) {
    if (it->source == source && it->bytes == bytes &&
        it->width == width && it->height == height &&
        it->numComponents == numComponents) {
      hasTransparency = it->hasTransparency;
      return it->copy;
    }
  }

  const bool carriesAlpha = numComponents == 2 || numComponents == 4;
  bool transparent = false;
  if (carriesAlpha) {
    const size_t pixelCount = bytes / static_cast<size_t>(numComponents);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
      if (source[pixel * static_cast<size_t>(numComponents) +
                  static_cast<size_t>(numComponents - 1)] != 0xffu) {
        transparent = true;
        break;
      }
    }
  }

  unsigned char * copy = static_cast<unsigned char *>(
    PRIVATE(this)->geometryPool.allocate(bytes, alignof(unsigned char)));
  std::memcpy(copy, source, bytes);
  SoIRRenderActionP::TextureStorage storage;
  storage.source = source;
  storage.bytes = bytes;
  storage.width = width;
  storage.height = height;
  storage.numComponents = numComponents;
  storage.copy = copy;
  storage.hasTransparency = transparent;
  PRIVATE(this)->textureStorage.push_back(storage);
  hasTransparency = transparent;
  return copy;
}

void
SoIRRenderAction::applyRenderStage(SoRenderCommand & command)
{
  if (PRIVATE(this)->renderStage == SoRenderStage::Background) {
    command.stage = SoRenderStage::Background;
  }
  else if (PRIVATE(this)->renderStage == SoRenderStage::AfterMain) {
    command.stage = SoRenderStage::AfterMain;
  }
  else if (PRIVATE(this)->renderStage == SoRenderStage::Foreground ||
           SoRenderPlacementElement::getLayer(this->state) ==
             SoRenderPlacementElement::FOREGROUND) {
    command.stage = SoRenderStage::Foreground;
  }
}

void
SoIRRenderAction::requestDepthClear()
{
  SoDepthClearEvent event;
  event.stage = PRIVATE(this)->renderStage;
  if (SoRenderPlacementElement::getLayer(this->state) ==
      SoRenderPlacementElement::FOREGROUND) {
    event.stage = SoRenderStage::Foreground;
  }
  event.sequence = static_cast<uint32_t>(
    this->drawlist.getNumCommands());
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  if (SoRenderPlacementElement::getViewport(
        this->state, x, y, width, height)) {
    event.viewportOverride = TRUE;
    event.viewportX = x;
    event.viewportY = y;
    event.viewportWidth = width;
    event.viewportHeight = height;
  }
  this->drawlist.addDepthClearEvent(event);
}

SoIRRenderStageScope::SoIRRenderStageScope(SoIRRenderAction & action,
                                           SoRenderStage stage)
  : action(&action), previousStage(action.getRenderStage())
{
  this->action->setRenderStage(stage);
}

SoIRRenderStageScope::~SoIRRenderStageScope()
{
  this->action->setRenderStage(this->previousStage);
}

SoRenderStage
SoIRRenderAction::getRenderStage() const
{
  return PRIVATE(this)->renderStage;
}

void
SoIRRenderAction::setRenderStage(SoRenderStage stage)
{
  PRIVATE(this)->renderStage = stage;
}

void
SoIRRenderAction::resetFrameResources()
{
  PRIVATE(this)->geometryPool.clear();
  PRIVATE(this)->textureStorage.clear();
  PRIVATE(this)->collectorStack.truncate(0);
  PRIVATE(this)->instancePathNodes.clear();
  PRIVATE(this)->instancePathIndices.clear();
  PRIVATE(this)->currentInstanceId = 0;
  PRIVATE(this)->nextInstanceId = 1;
}

void
SoIRRenderAction::clearCommandPaths()
{
  for (SoPath * path : this->commandPaths) {
    if (path && SoDB::isInitialized()) path->unref();
  }
  this->commandPaths.clear();
  PRIVATE(this)->renderStage = SoRenderStage::Main;
  PRIVATE(this)->hasRenderContextOverride = false;
  PRIVATE(this)->renderContextOverride = SoIRRenderContext();
}
