// src/actions/SoIRRenderAction.cpp

#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGetMatrixAction.h>

#include "rendering/SoRenderPlan.h"

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
#include <Inventor/elements/SoRenderMatrixPolicyElement.h>
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
#include <unordered_map>
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

  struct DependencyLink {
    size_t commandIndex;
    size_t next;
  };

  SoIRRenderActionP() = default;

  SoIRBuffer geometryPool;
  std::vector<TextureStorage> textureStorage;
  SbList<SoIRRenderAction::PrimitiveCollector *> collectorStack;
  std::unordered_multimap<uint64_t, SoGeometryHandle> geometrySources;
  bool constructionTimingEnabled = false;
  SoIRRenderAction::ConstructionStatistics constructionStatistics;
  std::vector<SbBool> commandAuthoredVisibility;
  // A changed state node affects commands below its parent branch. Indexing
  // that branch keeps incremental updates proportional to the affected
  // subtree instead of the size of the complete retained frame. Commands are
  // still checked against their full path because a node can be instanced in
  // more than one scene-graph location.
  std::unordered_map<SoNode *, size_t> branchDependencyHeads;
  std::vector<DependencyLink> branchDependencyLinks;
  bool recordBranchDependencies = true;
  SoRenderStage renderStage = SoRenderStage::Main;
  SoIRRenderContext renderContextOverride;
  bool hasRenderContextOverride = false;
};

#define PRIVATE(obj) ((obj)->pimpl)

class SoRetainedMutationTransaction {
public:
  explicit SoRetainedMutationTransaction(SoIRRenderAction & action)
    : action(action),
      drawListCheckpoint(action.drawlist.createMutationCheckpoint()),
      pathCount(action.commandPaths.size()),
      unsupported(action.unsupportedRendering),
      unsupportedNode(action.unsupportedNode),
      unsupportedReason(action.unsupportedReason),
      geometryCheckpoint(PRIVATE(&action)->geometryPool.checkpoint()),
      recordingDependencies(PRIVATE(&action)->recordBranchDependencies)
  {
    PRIVATE(&this->action)->recordBranchDependencies = false;
  }

  ~SoRetainedMutationTransaction()
  {
    if (this->active) this->finish(false);
  }

  int originalCommandCount() const
  { return this->drawListCheckpoint.commandCount; }
  size_t originalPathCount() const { return this->pathCount; }

  void finish(bool keepAllocations)
  {
    if (!this->active) return;
    PRIVATE(&this->action)->recordBranchDependencies =
      this->recordingDependencies;
    this->action.drawlist.restoreMutationCheckpoint(this->drawListCheckpoint);
    while (this->action.commandPaths.size() > this->pathCount) {
      SoPath * path = this->action.commandPaths.back();
      if (path) path->unref();
      this->action.commandPaths.pop_back();
    }
    PRIVATE(&this->action)->commandAuthoredVisibility.resize(this->pathCount);
    for (auto it = PRIVATE(&this->action)->geometrySources.begin();
         it != PRIVATE(&this->action)->geometrySources.end();) {
      if (it->second > static_cast<SoGeometryHandle>(
            this->drawListCheckpoint.geometryResourceCount)) {
        it = PRIVATE(&this->action)->geometrySources.erase(it);
      }
      else ++it;
    }
    this->action.unsupportedRendering = this->unsupported;
    this->action.unsupportedNode = this->unsupportedNode;
    this->action.unsupportedReason = this->unsupportedReason;
    if (!keepAllocations) {
      PRIVATE(&this->action)->geometryPool.rewind(this->geometryCheckpoint);
    }
    this->active = false;
  }

private:
  SoIRRenderAction & action;
  SoDrawList::MutationCheckpoint drawListCheckpoint;
  size_t pathCount;
  bool unsupported;
  const SoNode * unsupportedNode;
  const char * unsupportedReason;
  SoIRBuffer::Checkpoint geometryCheckpoint;
  bool recordingDependencies;
  bool active = true;
};

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
  SO_ENABLE(SoIRRenderAction, SoRenderMatrixPolicyElement);
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
  SoRenderCommand & retained = command;
  if (retained.objectId == 0) {
    const SoPath * currentPath = this->getCurPath();
    SoNode * tail = currentPath ? currentPath->getTail() : nullptr;
    if (tail) retained.objectId = tail->getNodeId();
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

  if (retained.geometry.cacheKey != 0) {
    retained.geometryHandle = this->findGeometrySource(
      retained.geometry.cacheKey, retained.geometry.revision);
    if (retained.geometryHandle == SO_INVALID_GEOMETRY_HANDLE) {
      SoGeometryResource resource;
      resource.geometry = retained.geometry;
      resource.sourceKey = retained.geometry.cacheKey;
      resource.revision = retained.geometry.revision;
      resource.elementRanges = retained.pick.elementRanges;
      retained.geometryHandle = this->drawlist.addGeometryResource(resource);
      retained.pick.elementRanges.clear();
      retained.pick.useResourceElementRanges = true;
      PRIVATE(this)->geometrySources.emplace(
        retained.geometry.cacheKey, retained.geometryHandle);
    }
    else {
      const SoGeometryResource * resource =
        this->drawlist.getGeometryResource(retained.geometryHandle);
      const auto rangesEqual = [](const SoRenderElementRange & lhs,
                                  const SoRenderElementRange & rhs) {
        return lhs.type == rhs.type && lhs.elementIndex == rhs.elementIndex &&
          lhs.drawStart == rhs.drawStart && lhs.drawCount == rhs.drawCount;
      };
      if (resource && resource->elementRanges.size() ==
                        retained.pick.elementRanges.size() &&
          std::equal(resource->elementRanges.begin(),
                     resource->elementRanges.end(),
                     retained.pick.elementRanges.begin(), rangesEqual)) {
        retained.pick.elementRanges.clear();
        retained.pick.useResourceElementRanges = true;
      }
    }
  }

  const int commandIndex = this->drawlist.getNumCommands();
  PRIVATE(this)->commandAuthoredVisibility.push_back(
    retained.state.raster.visible);
  this->drawlist.addCommand(std::move(retained));

  const SoPath * currentPath = this->getCurPath();
  // A retained frame owns a snapshot of each command path. Keep the nodes
  // referenced, but do not audit later scene-graph edits: those edits belong
  // to a subsequent frame and registering auditors is costly for dense scenes.
  SoPath * retainedPath = currentPath
    ? currentPath->copyWithAuditing(0, 0, FALSE) : NULL;
  if (retainedPath) retainedPath->ref();
  if (static_cast<size_t>(commandIndex) >= this->commandPaths.size()) {
    this->commandPaths.resize(static_cast<size_t>(commandIndex) + 1, NULL);
  }
  this->commandPaths[static_cast<size_t>(commandIndex)] = retainedPath;

  if (retainedPath && PRIVATE(this)->recordBranchDependencies) {
    const size_t noDependency = std::numeric_limits<size_t>::max();
    const int pathLength = retainedPath->getFullLength();
    for (int i = 0; i + 1 < pathLength; ++i) {
      SoNode * branchNode = static_cast<SoNode *>(retainedPath->nodes[i]);
      const auto inserted = PRIVATE(this)->branchDependencyHeads.emplace(
        branchNode, noDependency);
      SoIRRenderActionP::DependencyLink link = {
        static_cast<size_t>(commandIndex), inserted.first->second
      };
      PRIVATE(this)->branchDependencyLinks.push_back(link);
      inserted.first->second =
        PRIVATE(this)->branchDependencyLinks.size() - 1;
    }
  }
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
  SoRenderMatrixPolicyElement::set(
    this->state, nullptr,
    policy == CameraPolicy::CAMERA_IN_ROOT
      ? SoRenderMatrixPolicyElement::CAPTURE_CURRENT_MATRICES
      : SoRenderMatrixPolicyElement::INHERIT_CAMERA_MATRICES);
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
    SoRenderMatrixPolicyElement::set(
      this->state, nullptr,
      SoRenderMatrixPolicyElement::CAPTURE_CURRENT_MATRICES);
  }
  else {
    SoViewportRegionElement::set(this->state, this->vpRegion);
    SoDevicePixelRatioElement::set(this->state, this->devicePixelRatio);
    this->initializeCameraState(this->cameraPolicy);
    SoRenderMatrixPolicyElement::set(
      this->state, nullptr,
      this->cameraPolicy == CameraPolicy::CAMERA_IN_ROOT
        ? SoRenderMatrixPolicyElement::CAPTURE_CURRENT_MATRICES
        : SoRenderMatrixPolicyElement::INHERIT_CAMERA_MATRICES);
  }
  this->switchToPathTraversal(path);
  this->state->pop();
  PRIVATE(this)->hasRenderContextOverride = previousHasContext;
  if (previousHasContext) {
    PRIVATE(this)->renderContextOverride = previousContext;
  }
}

SoRenderStage
SoIRRenderAction::getRenderStage() const
{
  return PRIVATE(this)->renderStage;
}

void
SoIRRenderAction::setRenderStage(const SoRenderStage stage)
{
  PRIVATE(this)->renderStage = stage;
}

const SoIRRenderContext *
SoIRRenderAction::getRenderContextOverride() const
{
  return PRIVATE(this)->hasRenderContextOverride
    ? &PRIVATE(this)->renderContextOverride : nullptr;
}

int
SoIRRenderAction::updateCommandMatricesForStatePath(const SoPath * statePath)
{
  std::vector<const SoPath *> statePaths(1, statePath);
  return this->updateCommandMatricesForStatePaths(statePaths);
}

int
SoIRRenderAction::updateCommandMatricesForStatePaths(
  const std::vector<const SoPath *> & statePaths)
{
  std::vector<size_t> commandIndices;
  this->findCommandsAffectedByStatePaths(statePaths, commandIndices);
  SoGetMatrixAction matrixAction(this->vpRegion);
  std::vector<SbMatrix> replacements;
  replacements.reserve(commandIndices.size());
  for (size_t commandIndex : commandIndices) {
    const SoPath * commandPath = this->commandPaths[commandIndex];
    if (!commandPath) return 0;
    matrixAction.apply(const_cast<SoPath *>(commandPath));
    replacements.push_back(matrixAction.getMatrix());
  }
  for (size_t i = 0; i < commandIndices.size(); ++i) {
    SoRenderCommand & command = this->drawlist.getCommandForRetainedUpdate(
      static_cast<int>(commandIndices[i]));
    command.modelMatrix = replacements[i];
  }
  return static_cast<int>(commandIndices.size());
}

SbBool
SoIRRenderAction::transformUpdateAffectsPlanning(
  const std::vector<const SoPath *> & statePaths) const
{
  std::vector<size_t> commandIndices;
  this->findCommandsAffectedByStatePaths(statePaths, commandIndices);
  for (size_t commandIndex : commandIndices) {
    const SoRenderCommand & command = this->drawlist.getCommand(
      static_cast<int>(commandIndex));
    if (SoRenderCommandTraits::transformAffectsPlanning(command)) return TRUE;
  }
  return FALSE;
}

void
SoIRRenderAction::findCommandsAffectedByStatePaths(
  const std::vector<const SoPath *> & statePaths,
  std::vector<size_t> & commandIndices) const
{
  commandIndices.clear();
  std::vector<size_t> found;
  for (const SoPath * path : statePaths) {
    this->findCommandsAffectedByStatePath(path, found);
    commandIndices.insert(commandIndices.end(), found.begin(), found.end());
  }
  std::sort(commandIndices.begin(), commandIndices.end());
  commandIndices.erase(
    std::unique(commandIndices.begin(), commandIndices.end()),
    commandIndices.end());
}

void
SoIRRenderAction::findCommandsAffectedByStatePath(
  const SoPath * statePath, std::vector<size_t> & commandIndices) const
{
  commandIndices.clear();
  if (!statePath || statePath->getFullLength() < 2) return;
  // Additional-root traversal records paths below the scene root, while the
  // sensor path includes that root. A state node affects later siblings below
  // the same parent.
  const int statePathOffset = 1;
  const int prefixLength =
    statePath->getFullLength() - 1 - statePathOffset;
  // Additional-root paths omit the applied root. Without one retained
  // ancestor below it, replay cannot reconstruct root-level sibling state.
  if (prefixLength <= 0) return;
  const int changedStateIndex = statePath->getFullLength() - 1;
  const int changedSiblingIndex = statePath->indices[changedStateIndex];
  SoNode * branchNode = static_cast<SoNode *>(
    statePath->nodes[statePathOffset + prefixLength - 1]);
  const auto branch = PRIVATE(this)->branchDependencyHeads.find(branchNode);
  if (branch == PRIVATE(this)->branchDependencyHeads.end()) return;
  const size_t noDependency = std::numeric_limits<size_t>::max();
  for (size_t linkIndex = branch->second; linkIndex != noDependency;
       linkIndex = PRIVATE(this)->branchDependencyLinks[linkIndex].next) {
    const size_t commandIndex =
      PRIVATE(this)->branchDependencyLinks[linkIndex].commandIndex;
    const SoPath * commandPath = this->commandPaths[commandIndex];
    if (!commandPath || commandPath->getFullLength() <= prefixLength ||
        commandPath->indices[prefixLength] <= changedSiblingIndex) continue;
    bool matches = true;
    for (int i = 0; matches && i < prefixLength; ++i) {
      matches = commandPath->nodes[i] == statePath->nodes[statePathOffset + i] &&
        commandPath->indices[i] == statePath->indices[statePathOffset + i];
    }
    if (matches) commandIndices.push_back(commandIndex);
  }
}

namespace {
struct MaterialReplay {
  int materialIndex;
  SoMaterialData material;
};

SoCallbackAction::Response
captureEffectiveMaterial(void * data, SoCallbackAction * action,
                         const SoNode *)
{
  MaterialReplay * replay = static_cast<MaterialReplay *>(data);
  SoRenderIR::fillMaterialFromState(
    action->getState(), replay->material, replay->materialIndex);
  return SoCallbackAction::ABORT;
}
}

int
SoIRRenderAction::updateCommandDiffuseColorsForStatePath(
  const SoPath * statePath)
{
  std::vector<const SoPath *> statePaths(1, statePath);
  return this->updateCommandDiffuseColorsForStatePaths(statePaths);
}

int
SoIRRenderAction::updateCommandDiffuseColorsForStatePaths(
  const std::vector<const SoPath *> & statePaths)
{
  std::vector<size_t> commandIndices;
  this->findCommandsAffectedByStatePaths(statePaths, commandIndices);
  std::vector<MaterialReplay> replacements;
  replacements.reserve(commandIndices.size());
  for (size_t commandIndex : commandIndices) {
    const SoPath * commandPath = this->commandPaths[commandIndex];
    if (!commandPath) return 0;
    const SoRenderCommand & command =
      static_cast<const SoDrawList &>(this->drawlist).getCommand(
        static_cast<int>(commandIndex));
    MaterialReplay replay = { command.materialIndex, SoMaterialData() };
    SoCallbackAction materialAction(this->vpRegion);
    materialAction.addPreTailCallback(captureEffectiveMaterial, &replay);
    materialAction.apply(const_cast<SoPath *>(commandPath));
    replacements.push_back(replay);
  }
  for (size_t i = 0; i < commandIndices.size(); ++i) {
    SoRenderCommand & command = this->drawlist.getCommandForRetainedUpdate(
      static_cast<int>(commandIndices[i]));
    const MaterialReplay & replay = replacements[i];
    command.material.diffuse[0] = replay.material.diffuse[0];
    command.material.diffuse[1] = replay.material.diffuse[1];
    command.material.diffuse[2] = replay.material.diffuse[2];
  }
  return static_cast<int>(commandIndices.size());
}

int
SoIRRenderAction::updateCommandVisibilityForSwitchPath(
  const SoPath * switchPath, SbBool visible)
{
  if (!switchPath || switchPath->getFullLength() < 2) return 0;
  const int pathOffset = 1;
  const int prefixLength = switchPath->getFullLength() - pathOffset;
  SoNode * switchNode = static_cast<SoNode *>(
    switchPath->nodes[switchPath->getFullLength() - 1]);
  const auto branch = PRIVATE(this)->branchDependencyHeads.find(switchNode);
  if (branch == PRIVATE(this)->branchDependencyHeads.end()) return 0;

  int updated = 0;
  const size_t noDependency = std::numeric_limits<size_t>::max();
  for (size_t linkIndex = branch->second; linkIndex != noDependency;
       linkIndex = PRIVATE(this)->branchDependencyLinks[linkIndex].next) {
    const size_t commandIndex =
      PRIVATE(this)->branchDependencyLinks[linkIndex].commandIndex;
    const SoPath * commandPath = this->commandPaths[commandIndex];
    if (!commandPath || commandPath->getFullLength() <= prefixLength) continue;
    bool matches = true;
    for (int i = 0; matches && i < prefixLength; ++i) {
      matches = commandPath->nodes[i] == switchPath->nodes[pathOffset + i] &&
        commandPath->indices[i] == switchPath->indices[pathOffset + i];
    }
    if (!matches) continue;
    SoRenderCommand & command =
      this->drawlist.getCommandForRetainedUpdate(
        static_cast<int>(commandIndex));
    command.state.raster.visible = visible &&
      PRIVATE(this)->commandAuthoredVisibility[commandIndex];
    ++updated;
  }
  return updated;
}

int
SoIRRenderAction::updateCommandGeometryForStatePath(
  const SoPath * statePath)
{
  std::vector<const SoPath *> statePaths(1, statePath);
  return this->updateCommandGeometryForStatePaths(statePaths);
}

int
SoIRRenderAction::updateCommandGeometryForStatePaths(
  const std::vector<const SoPath *> & statePaths)
{
  std::vector<size_t> commandIndices;
  this->findCommandsAffectedByStatePaths(statePaths, commandIndices);
  if (commandIndices.empty()) return 0;

  struct ResourceUpdate {
    SoGeometryHandle handle;
    size_t representativeCommand;
    SoGeometryResource original;
    SoGeometryResource replacement;
    std::vector<size_t> owners;
  };
  std::vector<ResourceUpdate> updates;
  std::unordered_map<SoGeometryHandle, size_t> updateByHandle;
  for (size_t commandIndex : commandIndices) {
    const SoRenderCommand & command =
      static_cast<const SoDrawList &>(this->drawlist).getCommand(
        static_cast<int>(commandIndex));
    const SoGeometryHandle handle = command.geometryHandle;
    if (updateByHandle.find(handle) != updateByHandle.end()) continue;
    const SoGeometryResource * resource =
      this->drawlist.getGeometryResource(handle);
    if (!resource || !this->commandPaths[commandIndex]) return 0;
    ResourceUpdate update = {
      handle, commandIndex, *resource, SoGeometryResource(), {}
    };
    updateByHandle.emplace(handle, updates.size());
    updates.push_back(update);
  }

  // A shared producer can appear more than once through the same parent.
  // SoPath cannot distinguish those notification occurrences, but each stable
  // handle does identify the complete set of commands that must move together.
  for (int commandIndex = 0;
       commandIndex < this->drawlist.getNumCommands(); ++commandIndex) {
    const SoGeometryHandle handle =
      static_cast<const SoDrawList &>(this->drawlist)
        .getCommand(commandIndex).geometryHandle;
    const auto found = updateByHandle.find(handle);
    if (found != updateByHandle.end()) {
      updates[found->second].owners.push_back(
        static_cast<size_t>(commandIndex));
    }
  }

  SoRetainedMutationTransaction transaction(*this);
  const int commandCount = transaction.originalCommandCount();
  const size_t pathCount = transaction.originalPathCount();
  bool replayedOneCommandPerResource = true;
  for (const ResourceUpdate & update : updates) {
    const int before = this->drawlist.getNumCommands();
    this->traverseAdditionalPath(const_cast<SoPath *>(
      this->commandPaths[update.representativeCommand]));
    if (this->drawlist.getNumCommands() != before + 1) {
      replayedOneCommandPerResource = false;
      break;
    }
  }
  bool valid = replayedOneCommandPerResource &&
    this->drawlist.getNumCommands() ==
      commandCount + static_cast<int>(updates.size()) &&
    this->commandPaths.size() == pathCount + updates.size() &&
    !this->unsupportedRendering;
  for (size_t i = 0; valid && i < updates.size(); ++i) {
    const SoRenderCommand & replayed =
      this->drawlist.getCommand(commandCount + static_cast<int>(i));
    const SoGeometryResource * replayedResource =
      this->drawlist.getGeometryResource(replayed.geometryHandle);
    valid = replayedResource != nullptr;
    if (valid) updates[i].replacement = *replayedResource;
  }

  const auto sameLayout = [](const SoGeometryDesc & lhs,
                             const SoGeometryDesc & rhs) {
    return lhs.topology == rhs.topology &&
      lhs.vertexCount == rhs.vertexCount &&
      lhs.normalCount == rhs.normalCount &&
      lhs.indexCount == rhs.indexCount &&
      lhs.vertexStride == rhs.vertexStride &&
      lhs.texcoordStride == rhs.texcoordStride &&
      (lhs.positions != nullptr) == (rhs.positions != nullptr) &&
      (lhs.normals != nullptr) == (rhs.normals != nullptr) &&
      (lhs.texcoords != nullptr) == (rhs.texcoords != nullptr) &&
      (lhs.colors != nullptr) == (rhs.colors != nullptr) &&
      (lhs.indices != nullptr) == (rhs.indices != nullptr);
  };
  const auto sameRange = [](const SoRenderElementRange & lhs,
                            const SoRenderElementRange & rhs) {
    return lhs.type == rhs.type && lhs.elementIndex == rhs.elementIndex &&
      lhs.drawStart == rhs.drawStart && lhs.drawCount == rhs.drawCount;
  };
  for (size_t i = 0; valid && i < updates.size(); ++i) {
    const SoGeometryResource & original = updates[i].original;
    const SoGeometryResource & replacement = updates[i].replacement;
    valid = sameLayout(original.geometry, replacement.geometry) &&
      original.elementRanges.size() == replacement.elementRanges.size() &&
      std::equal(original.elementRanges.begin(), original.elementRanges.end(),
                 replacement.elementRanges.begin(), sameRange);
  }

  transaction.finish(valid);
  if (!valid) return 0;

  // Keep successful replay allocations in the frame arena and redirect each
  // stable handle only after the complete transaction has validated.
  int updatedCommands = 0;
  for (ResourceUpdate & update : updates) {
    SoGeometryResource * destination =
      this->drawlist.getGeometryResource(update.handle);
    if (!destination) return 0;
    const uint64_t nextRevision = destination->revision + 1;
    const uint64_t sourceKey = destination->sourceKey;
    update.replacement.sourceKey = sourceKey;
    update.replacement.revision = nextRevision;
    update.replacement.geometry.cacheKey = sourceKey;
    update.replacement.geometry.revision = nextRevision;
    *destination = update.replacement;
    for (size_t commandIndex : update.owners) {
      SoRenderCommand & command =
        this->drawlist.getCommandForRetainedUpdate(
          static_cast<int>(commandIndex));
      command.geometry.cacheKey = sourceKey;
      command.geometry.revision = nextRevision;
      ++updatedCommands;
    }
  }
  return updatedCommands;
}

void
SoIRRenderAction::beginTraversal(SoNode * node)
{
  SoViewportRegionElement::set(this->state, this->vpRegion);
  SoDevicePixelRatioElement::set(this->state, this->devicePixelRatio);
  this->initializeCameraState(this->cameraPolicy);
  SoRenderMatrixPolicyElement::set(
    this->state, nullptr,
    this->cameraPolicy == CameraPolicy::CAMERA_IN_ROOT
      ? SoRenderMatrixPolicyElement::CAPTURE_CURRENT_MATRICES
      : SoRenderMatrixPolicyElement::INHERIT_CAMERA_MATRICES);
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

SoGeometryHandle
SoIRRenderAction::findGeometrySource(const uint64_t sourceKey,
                                     const uint64_t revision) const
{
  const auto candidates = PRIVATE(this)->geometrySources.equal_range(sourceKey);
  for (auto candidate = candidates.first; candidate != candidates.second;
       ++candidate) {
    const SoGeometryResource * resource =
      this->drawlist.getGeometryResource(candidate->second);
    if (resource && resource->revision == revision) return candidate->second;
  }
  return SO_INVALID_GEOMETRY_HANDLE;
}

void
SoIRRenderAction::setConstructionTimingEnabled(const SbBool enabled)
{
  PRIVATE(this)->constructionTimingEnabled = enabled != FALSE;
}

SbBool
SoIRRenderAction::isConstructionTimingEnabled() const
{
  return PRIVATE(this)->constructionTimingEnabled ? TRUE : FALSE;
}

const SoIRRenderAction::ConstructionStatistics &
SoIRRenderAction::getConstructionStatistics() const
{
  return PRIVATE(this)->constructionStatistics;
}

void
SoIRRenderAction::recordPrimitiveGenerationNanoseconds(uint64_t nanoseconds)
{
  PRIVATE(this)->constructionStatistics.primitiveGenerationNanoseconds +=
    nanoseconds;
}

void
SoIRRenderAction::recordGeometryPackingNanoseconds(uint64_t nanoseconds)
{
  PRIVATE(this)->constructionStatistics.geometryPackingNanoseconds +=
    nanoseconds;
}

void
SoIRRenderAction::recordCommandEmissionNanoseconds(uint64_t nanoseconds)
{
  PRIVATE(this)->constructionStatistics.commandEmissionNanoseconds +=
    nanoseconds;
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

void
SoIRRenderAction::resetFrameResources()
{
  PRIVATE(this)->geometryPool.clear();
  PRIVATE(this)->textureStorage.clear();
  PRIVATE(this)->collectorStack.truncate(0);
  PRIVATE(this)->geometrySources.clear();
  PRIVATE(this)->constructionStatistics = ConstructionStatistics();
}

void
SoIRRenderAction::clearCommandPaths()
{
  for (SoPath * path : this->commandPaths) {
    if (path && SoDB::isInitialized()) path->unref();
  }
  this->commandPaths.clear();
  PRIVATE(this)->commandAuthoredVisibility.clear();
  PRIVATE(this)->branchDependencyHeads.clear();
  PRIVATE(this)->branchDependencyLinks.clear();
  PRIVATE(this)->recordBranchDependencies = true;
  PRIVATE(this)->renderStage = SoRenderStage::Main;
  PRIVATE(this)->hasRenderContextOverride = false;
  PRIVATE(this)->renderContextOverride = SoIRRenderContext();
}
