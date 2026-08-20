// src/actions/SoIRRenderAction.cpp

#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGetMatrixAction.h>

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
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

SO_ACTION_SOURCE(SoIRRenderAction);

class SoIRRenderActionP {
public:
  struct GeometryRecipeCacheEntry {
    uint64_t recipeKey = 0;
    uint64_t resourceKey = 0;
    SoGeometryHandle handle = SO_INVALID_GEOMETRY_HANDLE;
  };

  struct GeometrySourceCacheEntry {
    uint64_t sourceId = 0;
    uint64_t revision = 0;
    SoGeometryHandle handle = SO_INVALID_GEOMETRY_HANDLE;
  };

  struct PathRecord {
    size_t first = 0;
    size_t length = 0;
  };

  struct DependencyLink {
    size_t commandIndex;
    size_t next;
  };

  struct DependencyHead {
    SoNode * node = nullptr;
    size_t link = std::numeric_limits<size_t>::max();
  };

  // Incremental invalidation needs one linked-list head per scene-graph
  // branch. A reusable open-addressed table avoids the node allocation and
  // pointer chasing of a general-purpose unordered map during every rebuild.
  DependencyHead * dependencyHead(SoNode * node, bool create)
  {
    if (this->branchDependencyHeads.empty()) {
      if (!create) return nullptr;
      this->branchDependencyHeads.resize(16);
    }
    if (create && (this->branchDependencyCount + 1) * 2 >
                    this->branchDependencyHeads.size()) {
      std::vector<DependencyHead> previous =
        std::move(this->branchDependencyHeads);
      this->branchDependencyHeads.assign(previous.size() * 2,
                                         DependencyHead());
      this->branchDependencyCount = 0;
      for (const DependencyHead & entry : previous) {
        if (!entry.node) continue;
        DependencyHead * destination = this->dependencyHead(entry.node, true);
        destination->link = entry.link;
      }
    }
    const size_t mask = this->branchDependencyHeads.size() - 1;
    size_t slot = (reinterpret_cast<uintptr_t>(node) >> 4) & mask;
    while (this->branchDependencyHeads[slot].node) {
      if (this->branchDependencyHeads[slot].node == node) {
        return &this->branchDependencyHeads[slot];
      }
      slot = (slot + 1) & mask;
    }
    if (!create) return nullptr;
    this->branchDependencyHeads[slot].node = node;
    ++this->branchDependencyCount;
    return &this->branchDependencyHeads[slot];
  }

  void resetDependencyHeads()
  {
    std::fill(this->branchDependencyHeads.begin(),
              this->branchDependencyHeads.end(), DependencyHead());
    this->branchDependencyCount = 0;
  }

  // An open-addressed table avoids one allocation per retained node. Keeping
  // it at most half full bounds probe length and lets later frames reuse it.
  bool retainPathNode(SoNode * node)
  {
    if (!node) return false;
    if (this->ownedPathNodeTable.empty()) {
      this->ownedPathNodeTable.resize(16, NULL);
      this->pathStatistics.estimatedStorageBytes +=
        this->ownedPathNodeTable.size() * sizeof(SoNode *);
    }
    if ((this->ownedPathNodes.size() + 1) * 2 >
        this->ownedPathNodeTable.size()) {
      const std::vector<SoNode *> previous =
        std::move(this->ownedPathNodeTable);
      this->ownedPathNodeTable.assign(previous.size() * 2, NULL);
      this->pathStatistics.estimatedStorageBytes +=
        previous.size() * sizeof(SoNode *);
      for (SoNode * owned : previous) {
        if (owned) this->insertPathNode(owned);
      }
    }
    if (!this->insertPathNode(node)) return false;
    node->ref();
    this->ownedPathNodes.push_back(node);
    return true;
  }

  void resetPathNodeTable()
  {
    std::fill(this->ownedPathNodeTable.begin(),
              this->ownedPathNodeTable.end(), nullptr);
  }

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
  SoInstanceId lastPathInstanceId = 0;
  SoIRRenderAction::PathStatistics pathStatistics;
  std::vector<PathRecord> commandPathRecords;
  std::vector<SoNode *> pathNodes;
  std::vector<int> pathIndices;
  std::vector<SoNode *> ownedPathNodes;
  std::vector<SoNode *> ownedPathNodeTable;
  std::vector<DependencyHead> branchDependencyHeads;
  size_t branchDependencyCount = 0;
  std::vector<DependencyLink> branchDependencyLinks;
  std::unordered_multimap<uint64_t, SoGeometryHandle> geometryRecipes;
  // Repeated instances revisit a small working set of geometry identities.
  // These allocation-free front caches avoid hashing the common command and
  // source lookups. Entries are identity-checked; collisions use the map.
  std::array<GeometryRecipeCacheEntry, 256> geometryRecipeCache{};
  std::array<GeometrySourceCacheEntry, 256> geometrySourceCache{};
  bool recordBranchDependencies = true;
  bool commandTimingEnabled = false;
  SoRenderStage renderStage = SoRenderStage::Main;
  SoIRRenderContext renderContextOverride;
  bool hasRenderContextOverride = false;

  static uint64_t mixGeometryCacheKey(uint64_t first, uint64_t second)
  {
    uint64_t mixed = first + UINT64_C(0x9e3779b97f4a7c15);
    mixed ^= second + UINT64_C(0x9e3779b97f4a7c15) +
      (mixed << 6) + (mixed >> 2);
    mixed = (mixed ^ (mixed >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    mixed = (mixed ^ (mixed >> 27)) * UINT64_C(0x94d049bb133111eb);
    return mixed ^ (mixed >> 31);
  }

  size_t geometryRecipeCacheSlot(uint64_t recipeKey,
                                 uint64_t resourceKey) const
  {
    const uint64_t mixed = mixGeometryCacheKey(recipeKey, resourceKey);
    return static_cast<size_t>(mixed) & (this->geometryRecipeCache.size() - 1);
  }

  size_t geometrySourceCacheSlot(uint64_t sourceId, uint64_t revision) const
  {
    const uint64_t mixed = mixGeometryCacheKey(sourceId, revision);
    return static_cast<size_t>(mixed) & (this->geometrySourceCache.size() - 1);
  }

private:
  bool insertPathNode(SoNode * node)
  {
    const size_t mask = this->ownedPathNodeTable.size() - 1;
    size_t slot = (reinterpret_cast<uintptr_t>(node) >> 4) & mask;
    while (this->ownedPathNodeTable[slot]) {
      if (this->ownedPathNodeTable[slot] == node) return false;
      slot = (slot + 1) & mask;
    }
    this->ownedPathNodeTable[slot] = node;
    return true;
  }
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
  using CommandClock = std::chrono::steady_clock;
  const bool timing = PRIVATE(this)->commandTimingEnabled;
  CommandClock::time_point phaseStart;
  if (timing) phaseStart = CommandClock::now();
  const auto finishPhase = [&](uint64_t & nanoseconds) {
    if (!timing) return;
    const CommandClock::time_point now = CommandClock::now();
    nanoseconds += static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        now - phaseStart).count());
    phaseStart = now;
  };
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
  if (currentPath) {
    SoIRRenderAction::PathStatistics & statistics =
      PRIVATE(this)->pathStatistics;
    ++statistics.commands;
    if (retained.instanceId != PRIVATE(this)->lastPathInstanceId) {
      ++statistics.uniquePaths;
      PRIVATE(this)->lastPathInstanceId = retained.instanceId;
    }
    else {
      ++statistics.reusedPaths;
    }
    const uint64_t length = static_cast<uint64_t>(currentPath->getFullLength());
    statistics.nodeEntries += length;
  }
  finishPhase(PRIVATE(this)->pathStatistics.commandPathIdentityNanoseconds);

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
  finishPhase(PRIVATE(this)->pathStatistics.commandStateNanoseconds);

  retained.geometryHandle = SO_INVALID_GEOMETRY_HANDLE;
  if (PRIVATE(this)->recordBranchDependencies &&
      retained.geometry.recipeKey != 0) {
    SoIRRenderAction::PathStatistics & statistics =
      PRIVATE(this)->pathStatistics;
    ++statistics.geometryRecipeLookupAttempts;
    const size_t cacheSlot = PRIVATE(this)->geometryRecipeCacheSlot(
      retained.geometry.recipeKey, retained.geometry.resourceKey);
    const SoIRRenderActionP::GeometryRecipeCacheEntry & cached =
      PRIVATE(this)->geometryRecipeCache[cacheSlot];
    if (retained.geometry.recipeKey == cached.recipeKey &&
        retained.geometry.resourceKey == cached.resourceKey) {
      const SoGeometryResource * resource = this->drawlist.getGeometryResource(
        cached.handle);
      if (resource && resource->geometry.resourceKey ==
                        retained.geometry.resourceKey) {
        retained.geometryHandle = cached.handle;
        ++statistics.geometryRecipeCacheHits;
      }
    }
    if (retained.geometryHandle == SO_INVALID_GEOMETRY_HANDLE) {
      ++statistics.geometryRecipeHashLookups;
      const std::pair<
        std::unordered_multimap<uint64_t, SoGeometryHandle>::const_iterator,
        std::unordered_multimap<uint64_t, SoGeometryHandle>::const_iterator>
        candidates = PRIVATE(this)->geometryRecipes.equal_range(
          retained.geometry.recipeKey);
      for (std::unordered_multimap<uint64_t, SoGeometryHandle>::const_iterator
           candidate = candidates.first;
           candidate != candidates.second; ++candidate) {
        ++statistics.geometryRecipeCandidatesScanned;
        const SoGeometryResource * resource =
          this->drawlist.getGeometryResource(candidate->second);
        if (resource && resource->geometry.resourceKey ==
                          retained.geometry.resourceKey) {
          retained.geometryHandle = candidate->second;
          break;
        }
      }
    }
  }
  if (retained.geometryHandle == SO_INVALID_GEOMETRY_HANDLE) {
    SoGeometryResource geometryResource;
    geometryResource.geometry = retained.geometry;
    geometryResource.sourceKey = retained.geometry.recipeKey;
    geometryResource.revision = retained.geometry.revision;
    geometryResource.elementRanges = std::move(retained.pick.elementRanges);
    retained.geometryHandle =
      this->drawlist.addGeometryResource(std::move(geometryResource));
    retained.pick.useResourceElementRanges = true;
    if (PRIVATE(this)->recordBranchDependencies &&
        retained.geometry.recipeKey != 0) {
      PRIVATE(this)->geometryRecipes.emplace(
        retained.geometry.recipeKey, retained.geometryHandle);
    }
  }
  else if (!retained.pick.useResourceElementRanges) {
    const SoGeometryResource * resource =
      this->drawlist.getGeometryResource(retained.geometryHandle);
    const auto rangesEqual = [](const SoRenderElementRange & lhs,
                                const SoRenderElementRange & rhs) {
      return lhs.type == rhs.type &&
        lhs.elementIndex == rhs.elementIndex &&
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
  if (PRIVATE(this)->recordBranchDependencies &&
      retained.geometry.recipeKey != 0) {
    const size_t cacheSlot = PRIVATE(this)->geometryRecipeCacheSlot(
      retained.geometry.recipeKey, retained.geometry.resourceKey);
    SoIRRenderActionP::GeometryRecipeCacheEntry & cached =
      PRIVATE(this)->geometryRecipeCache[cacheSlot];
    cached.recipeKey = retained.geometry.recipeKey;
    cached.resourceKey = retained.geometry.resourceKey;
    cached.handle = retained.geometryHandle;
    const size_t sourceSlot = PRIVATE(this)->geometrySourceCacheSlot(
      retained.geometry.recipeKey, retained.geometry.revision);
    SoIRRenderActionP::GeometrySourceCacheEntry & source =
      PRIVATE(this)->geometrySourceCache[sourceSlot];
    source.sourceId = retained.geometry.recipeKey;
    source.revision = retained.geometry.revision;
    source.handle = retained.geometryHandle;
  }
  finishPhase(PRIVATE(this)->pathStatistics.geometryResourceNanoseconds);
  const int commandIndex = this->drawlist.getNumCommands();
  this->drawlist.addCommand(std::move(retained));
  finishPhase(PRIVATE(this)->pathStatistics.drawListAppendNanoseconds);

  SoIRRenderActionP::PathRecord pathRecord;
  if (currentPath) {
    pathRecord.first = PRIVATE(this)->pathNodes.size();
    pathRecord.length = static_cast<size_t>(currentPath->getFullLength());
    void ** nodeArray = currentPath->nodes.getArrayPtr();
    const int * indexArray = currentPath->indices.getArrayPtr();
    for (size_t i = 0; i < pathRecord.length; ++i) {
      SoNode * node = static_cast<SoNode *>(nodeArray[i]);
      PRIVATE(this)->pathNodes.push_back(node);
      PRIVATE(this)->pathIndices.push_back(indexArray[i]);
      if (PRIVATE(this)->recordBranchDependencies &&
          i + 1 < pathRecord.length) {
        const size_t previousCount = PRIVATE(this)->branchDependencyCount;
        SoIRRenderActionP::DependencyHead * branch =
          PRIVATE(this)->dependencyHead(node, true);
        if (PRIVATE(this)->branchDependencyCount != previousCount) {
          ++PRIVATE(this)->pathStatistics.dependencyBranches;
          PRIVATE(this)->pathStatistics.dependencyEstimatedStorageBytes +=
            sizeof(SoNode *) + sizeof(size_t);
        }
        SoIRRenderActionP::DependencyLink dependency = {
          static_cast<size_t>(commandIndex), branch->link
        };
        PRIVATE(this)->branchDependencyLinks.push_back(dependency);
        branch->link = PRIVATE(this)->branchDependencyLinks.size() - 1;
        ++PRIVATE(this)->pathStatistics.dependencyCommandReferences;
        PRIVATE(this)->pathStatistics.dependencyEstimatedStorageBytes +=
          sizeof(dependency);
      }
      if (PRIVATE(this)->retainPathNode(node)) {
        ++PRIVATE(this)->pathStatistics.nodeReferences;
        PRIVATE(this)->pathStatistics.estimatedStorageBytes += sizeof(node);
      }
    }
    PRIVATE(this)->pathStatistics.estimatedStorageBytes +=
      sizeof(pathRecord) + pathRecord.length * (sizeof(SoNode *) + sizeof(int));
  }
  PRIVATE(this)->commandPathRecords.push_back(pathRecord);
  assert(PRIVATE(this)->commandPathRecords.size() ==
         static_cast<size_t>(commandIndex) + 1);
  finishPhase(PRIVATE(this)->pathStatistics.pathDependencyNanoseconds);
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
      static_cast<size_t>(commandIndex) >=
        PRIVATE(this)->commandPathRecords.size()) {
    return NULL;
  }
  const size_t index = static_cast<size_t>(commandIndex);
  if (index >= this->commandPaths.size()) {
    this->commandPaths.resize(index + 1, NULL);
  }
  if (this->commandPaths[index]) return this->commandPaths[index];

  const SoIRRenderActionP::PathRecord & record =
    PRIVATE(this)->commandPathRecords[index];
  if (record.length == 0) return NULL;
  SoPath * path = new SoPath(static_cast<int>(record.length));
  path->auditPath(FALSE);
  for (size_t i = 0; i < record.length; ++i) {
    const size_t entry = record.first + i;
    path->append(PRIVATE(this)->pathNodes[entry],
                 PRIVATE(this)->pathIndices[entry]);
  }
  path->ref();
  this->commandPaths[index] = path;
  return path;
}

const SoIRRenderAction::PathStatistics &
SoIRRenderAction::getPathStatistics(void) const
{
  return PRIVATE(this)->pathStatistics;
}

void
SoIRRenderAction::setCommandTimingEnabled(const SbBool enabled)
{
  PRIVATE(this)->commandTimingEnabled = enabled != FALSE;
}

SbBool
SoIRRenderAction::isCommandTimingEnabled() const
{
  return PRIVATE(this)->commandTimingEnabled ? TRUE : FALSE;
}

SoGeometryHandle
SoIRRenderAction::findGeometrySource(const uint64_t sourceId,
                                     const uint64_t revision) const
{
  SoIRRenderAction::PathStatistics & statistics =
    PRIVATE(this)->pathStatistics;
  ++statistics.geometrySourceLookupAttempts;
  const size_t cacheSlot = PRIVATE(this)->geometrySourceCacheSlot(
    sourceId, revision);
  const SoIRRenderActionP::GeometrySourceCacheEntry & cached =
    PRIVATE(this)->geometrySourceCache[cacheSlot];
  if (cached.sourceId == sourceId && cached.revision == revision) {
    const SoGeometryResource * resource =
      this->drawlist.getGeometryResource(cached.handle);
    if (resource && resource->revision == revision) {
      ++statistics.geometrySourceCacheHits;
      return cached.handle;
    }
  }
  ++statistics.geometrySourceHashLookups;
  const std::pair<
    std::unordered_multimap<uint64_t, SoGeometryHandle>::const_iterator,
    std::unordered_multimap<uint64_t, SoGeometryHandle>::const_iterator>
    candidates = PRIVATE(this)->geometryRecipes.equal_range(sourceId);
  for (std::unordered_multimap<uint64_t, SoGeometryHandle>::const_iterator
       candidate = candidates.first; candidate != candidates.second;
       ++candidate) {
    ++statistics.geometrySourceCandidatesScanned;
    const SoGeometryResource * resource =
      this->drawlist.getGeometryResource(candidate->second);
    if (resource && resource->revision == revision) return candidate->second;
  }
  return SO_INVALID_GEOMETRY_HANDLE;
}

void
SoIRRenderAction::recordPrimitiveGenerationNanoseconds(uint64_t nanoseconds)
{
  PRIVATE(this)->pathStatistics.primitiveGenerationNanoseconds += nanoseconds;
}

void
SoIRRenderAction::recordGeometryPackingNanoseconds(uint64_t nanoseconds)
{
  PRIVATE(this)->pathStatistics.geometryPackingNanoseconds += nanoseconds;
}

void
SoIRRenderAction::recordCommandEmissionNanoseconds(uint64_t nanoseconds)
{
  PRIVATE(this)->pathStatistics.commandEmissionNanoseconds += nanoseconds;
}

void
SoIRRenderAction::recordCommandGeometryIdentityNanoseconds(uint64_t nanoseconds)
{
  PRIVATE(this)->pathStatistics.commandGeometryIdentityNanoseconds += nanoseconds;
}

void
SoIRRenderAction::recordCommandStateCaptureNanoseconds(uint64_t nanoseconds)
{
  PRIVATE(this)->pathStatistics.commandStateCaptureNanoseconds += nanoseconds;
}

void
SoIRRenderAction::recordCommandFinalizationNanoseconds(uint64_t nanoseconds)
{
  PRIVATE(this)->pathStatistics.commandFinalizationNanoseconds += nanoseconds;
}

void
SoIRRenderAction::recordCommandPickingMetadataNanoseconds(uint64_t nanoseconds)
{
  PRIVATE(this)->pathStatistics.commandPickingMetadataNanoseconds += nanoseconds;
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

int
SoIRRenderAction::updateCommandMatricesForStatePath(const SoPath * statePath)
{
  std::vector<size_t> commandIndices;
  this->findCommandsAffectedByStatePath(statePath, commandIndices);
  int updated = 0;
  for (std::vector<size_t>::const_iterator it = commandIndices.begin();
       it != commandIndices.end(); ++it) {
    const SoPath * commandPath = this->getCommandPath(static_cast<int>(*it));
    if (!commandPath) continue;
    SoGetMatrixAction matrixAction(this->vpRegion);
    matrixAction.apply(const_cast<SoPath *>(commandPath));
    this->drawlist.getCommand(static_cast<int>(*it)).modelMatrix =
      matrixAction.getMatrix();
    ++updated;
  }
  return updated;
}

void
SoIRRenderAction::findCommandsAffectedByStatePath(
  const SoPath * statePath, std::vector<size_t> & commandIndices) const
{
  commandIndices.clear();
  if (!statePath || statePath->getFullLength() < 2) return;
  // Paths recorded while applying the scene root begin at its child. Match the
  // affected branch below that root; the changed state node itself is a left
  // sibling of the recorded shape path.
  const size_t statePathOffset = 1;
  const size_t prefixLength =
    static_cast<size_t>(statePath->getFullLength() - 1) - statePathOffset;
  // The applied scene root is not part of recorded command paths. Replaying a
  // root-level state sibling would therefore omit that state; use a full frame
  // rebuild until retained paths carry the applied root explicitly.
  if (prefixLength == 0) return;
  void ** prefixNodes = statePath->nodes.getArrayPtr();
  const int * prefixIndices = statePath->indices.getArrayPtr();
  const int changedChildIndex =
    prefixIndices[statePath->getFullLength() - 1];
  SoNode * branch = static_cast<SoNode *>(
    prefixNodes[statePathOffset + prefixLength - 1]);
  const SoIRRenderActionP::DependencyHead * found =
    PRIVATE(this)->dependencyHead(branch, false);
  if (!found) return;
  const size_t noDependency = std::numeric_limits<size_t>::max();
  for (size_t linkIndex = found->link; linkIndex != noDependency;
       linkIndex = PRIVATE(this)->branchDependencyLinks[linkIndex].next) {
    const size_t commandIndex =
      PRIVATE(this)->branchDependencyLinks[linkIndex].commandIndex;
    const SoIRRenderActionP::PathRecord & record =
      PRIVATE(this)->commandPathRecords[commandIndex];
    if (record.length <= prefixLength ||
        PRIVATE(this)->pathIndices[record.first + prefixLength] <=
          changedChildIndex) {
      continue;
    }
    bool matches = true;
    for (size_t i = 0; matches && i < prefixLength; ++i) {
      const size_t entry = record.first + i;
      matches =
        PRIVATE(this)->pathNodes[entry] == prefixNodes[statePathOffset + i] &&
        PRIVATE(this)->pathIndices[entry] ==
          prefixIndices[statePathOffset + i];
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

void
copyGeometryPayload(const SoGeometryDesc & source,
                    const SoGeometryDesc & destination)
{
  if (source.positions) std::memcpy(
    const_cast<float *>(destination.positions), source.positions,
    static_cast<size_t>(source.vertexCount) * 3 * sizeof(float));
  if (source.normals) std::memcpy(
    const_cast<float *>(destination.normals), source.normals,
    static_cast<size_t>(source.normalCount) * 3 * sizeof(float));
  if (source.texcoords) std::memcpy(
    const_cast<float *>(destination.texcoords), source.texcoords,
    static_cast<size_t>(source.vertexCount) * 4 * sizeof(float));
  if (source.colors) std::memcpy(
    const_cast<float *>(destination.colors), source.colors,
    static_cast<size_t>(source.vertexCount) * 4 * sizeof(float));
  if (source.indices) std::memcpy(
    const_cast<uint32_t *>(destination.indices), source.indices,
    static_cast<size_t>(source.indexCount) * sizeof(uint32_t));
}
}

int
SoIRRenderAction::updateCommandMaterialsForStatePath(
  const SoPath * statePath, bool opacityMayChange)
{
  std::vector<size_t> commandIndices;
  this->findCommandsAffectedByStatePath(statePath, commandIndices);
  std::vector<MaterialReplay> replacements;
  replacements.reserve(commandIndices.size());
  for (std::vector<size_t>::const_iterator it = commandIndices.begin();
       it != commandIndices.end(); ++it) {
    const SoPath * commandPath = this->getCommandPath(static_cast<int>(*it));
    if (!commandPath) return 0;
    const SoRenderCommand & command =
      static_cast<const SoDrawList &>(this->drawlist).getCommand(
        static_cast<int>(*it));
    // Vertex and texture alpha may already contain material opacity. Rebuilding
    // is safer than trying to undo and recompute that composed alpha in place.
    if (opacityMayChange &&
        (command.geometry.colors != NULL ||
         command.material.textureAlphaIncludesOpacity ||
         command.material.vertexColorAlphaIncludesOpacity)) {
      return 0;
    }
    MaterialReplay replay = {
      command.materialIndex, SoMaterialData()
    };
    SoCallbackAction materialAction(this->vpRegion);
    materialAction.addPreTailCallback(captureEffectiveMaterial, &replay);
    materialAction.apply(const_cast<SoPath *>(commandPath));
    replay.material.texture = command.material.texture;
    replay.material.textureAlphaIncludesOpacity =
      command.material.textureAlphaIncludesOpacity;
    replay.material.vertexColorAlphaIncludesOpacity =
      command.material.vertexColorAlphaIncludesOpacity;
    replacements.push_back(replay);
  }

  // Do not alter any retained command until every replay has succeeded.
  for (size_t i = 0; i < commandIndices.size(); ++i) {
    SoRenderCommand & command = this->drawlist.getCommand(
      static_cast<int>(commandIndices[i]));
    command.material = replacements[i].material;
    // Remove only blend state synthesized by the previous finalization pass.
    // Explicit scene-authored blending must survive a material update.
    if (command.finalizationEnabledBlend) command.state.blend = SoBlendState();
    SoRenderIR::finalizeCommand(command);
  }
  return static_cast<int>(commandIndices.size());
}

int
SoIRRenderAction::updateCommandGeometryForStatePath(
  const SoPath * statePath)
{
  std::vector<size_t> commandIndices;
  this->findCommandsAffectedByStatePath(statePath, commandIndices);
  if (commandIndices.empty()) return 0;
  std::sort(commandIndices.begin(), commandIndices.end());

  const int originalCommandCount = this->drawlist.getNumCommands();
  const int originalResourceCount =
    this->drawlist.getNumGeometryResources();
  const size_t originalRecordCount =
    PRIVATE(this)->commandPathRecords.size();
  const size_t originalNodeCount = PRIVATE(this)->pathNodes.size();
  const PathStatistics originalStatistics = PRIVATE(this)->pathStatistics;
  const bool originalUnsupported = this->unsupportedRendering;
  const SoNode * originalUnsupportedNode = this->unsupportedNode;
  const char * originalUnsupportedReason = this->unsupportedReason;
  const SoIRBuffer::Checkpoint geometryCheckpoint =
    PRIVATE(this)->geometryPool.checkpoint();

  const SoGeometryHandle sharedHandle =
    this->drawlist.getCommand(static_cast<int>(commandIndices.front()))
      .geometryHandle;
  bool sharedRecipe = sharedHandle != SO_INVALID_GEOMETRY_HANDLE;
  for (size_t i = 1; sharedRecipe && i < commandIndices.size(); ++i) {
    sharedRecipe = this->drawlist.getCommand(
      static_cast<int>(commandIndices[i])).geometryHandle == sharedHandle;
  }
  const size_t replacementCount = sharedRecipe ? 1 : commandIndices.size();

  PRIVATE(this)->recordBranchDependencies = false;
  bool replacementShapeMatches = true;
  const int changedChildIndex = statePath->getIndex(
    statePath->getFullLength() - 1);
  if (sharedRecipe) {
    const SoPath * commandPath = this->getCommandPath(
      static_cast<int>(commandIndices.front()));
    if (commandPath) {
      this->traverseAdditionalPath(const_cast<SoPath *>(commandPath));
      replacementShapeMatches = this->drawlist.getNumCommands() ==
        originalCommandCount + 1;
    }
    else replacementShapeMatches = false;
  }
  else if (changedChildIndex == 0) {
    const SoPath * firstCommandPath = this->getCommandPath(
      static_cast<int>(commandIndices.front()));
    const int branchPathLength = statePath->getFullLength() - 2;
    if (firstCommandPath && branchPathLength > 0 &&
        firstCommandPath->getFullLength() > branchPathLength) {
      SoPath * branchPath = firstCommandPath->copy(0, branchPathLength);
      branchPath->ref();
      // Traversing the common branch reconstructs its ancestor state once and
      // then visits every affected shape in normal scene-graph order.
      this->traverseAdditionalPath(branchPath);
      branchPath->unref();
      replacementShapeMatches = this->drawlist.getNumCommands() ==
        originalCommandCount + static_cast<int>(replacementCount);
    }
    else replacementShapeMatches = false;
  }
  else {
    for (std::vector<size_t>::const_iterator it = commandIndices.begin();
         replacementShapeMatches && it != commandIndices.end(); ++it) {
      const SoPath * commandPath = this->getCommandPath(static_cast<int>(*it));
      if (!commandPath) {
        replacementShapeMatches = false;
        break;
      }
      const int before = this->drawlist.getNumCommands();
      this->traverseAdditionalPath(const_cast<SoPath *>(commandPath));
      replacementShapeMatches = this->drawlist.getNumCommands() == before + 1;
    }
  }
  PRIVATE(this)->recordBranchDependencies = true;
  bool replacementsCompatible = replacementShapeMatches &&
    this->drawlist.getNumCommands() ==
      originalCommandCount + static_cast<int>(replacementCount) &&
    PRIVATE(this)->commandPathRecords.size() ==
      originalRecordCount + replacementCount &&
    !this->unsupportedRendering;

  for (size_t i = 0; replacementsCompatible && i < commandIndices.size(); ++i) {
    const SoRenderCommand & original = this->drawlist.getCommand(
      static_cast<int>(commandIndices[i]));
    const SoRenderCommand & replacement = this->drawlist.getCommand(
      originalCommandCount + static_cast<int>(sharedRecipe ? 0 : i));
    const SoGeometryDesc & source = replacement.geometry;
    const SoGeometryDesc & destination = original.geometry;
    replacementsCompatible = source.topology == destination.topology &&
      source.vertexCount == destination.vertexCount &&
      source.normalCount == destination.normalCount &&
      source.indexCount == destination.indexCount &&
      source.vertexStride == destination.vertexStride &&
      source.vertexStride == 3 * sizeof(float) &&
      source.texcoordStride == destination.texcoordStride &&
      source.texcoordStride == 4 * sizeof(float) &&
      (source.positions != NULL) == (destination.positions != NULL) &&
      (source.normals != NULL) == (destination.normals != NULL) &&
      (source.texcoords != NULL) == (destination.texcoords != NULL) &&
      (source.colors != NULL) == (destination.colors != NULL) &&
      (source.indices != NULL) == (destination.indices != NULL) &&
      replacement.material.texture.numComponents == 0;
  }

  if (replacementsCompatible) {
    const SoRenderCommand & firstReplacement = this->drawlist.getCommand(
      originalCommandCount);
    const SoGeometryDesc & source = firstReplacement.geometry;
    SoRenderCommand & firstOriginal = this->drawlist.getCommand(
      static_cast<int>(commandIndices.front()));
    const SoGeometryDesc destination = firstOriginal.geometry;
    if (sharedRecipe) copyGeometryPayload(source, destination);
    for (size_t i = 0; i < commandIndices.size(); ++i) {
      SoRenderCommand & original = this->drawlist.getCommand(
        static_cast<int>(commandIndices[i]));
      const SoRenderCommand & replacement = this->drawlist.getCommand(
        originalCommandCount + static_cast<int>(sharedRecipe ? 0 : i));
      const SoGeometryDesc & replacementSource = replacement.geometry;
      const SoGeometryDesc originalDestination = original.geometry;
      if (!sharedRecipe) {
        copyGeometryPayload(replacementSource, originalDestination);
      }
      SoRenderCommand retainedReplacement = sharedRecipe
        ? original : replacement;
      if (sharedRecipe) {
        retainedReplacement.geometry = replacementSource;
        retainedReplacement.pick = replacement.pick;
      }
      retainedReplacement.geometryHandle = original.geometryHandle;
      const SoGeometryDesc & retainedStorage = sharedRecipe
        ? destination : originalDestination;
      retainedReplacement.geometry.positions = retainedStorage.positions;
      retainedReplacement.geometry.normals = retainedStorage.normals;
      retainedReplacement.geometry.texcoords = retainedStorage.texcoords;
      retainedReplacement.geometry.colors = retainedStorage.colors;
      retainedReplacement.geometry.indices = retainedStorage.indices;
      original = std::move(retainedReplacement);
      SoGeometryResource * resource =
        this->drawlist.getGeometryResource(original.geometryHandle);
      if (resource) {
        resource->geometry = original.geometry;
        resource->sourceKey = original.geometry.recipeKey;
        resource->revision = original.geometry.revision;
      }
    }
  }

  // The compatible payload was copied into the original command's storage.
  // Discard the temporary command, path metadata, and scratch allocations.
  this->drawlist.truncate(originalCommandCount);
  this->drawlist.truncateGeometryResources(originalResourceCount);
  PRIVATE(this)->commandPathRecords.resize(originalRecordCount);
  PRIVATE(this)->pathNodes.resize(originalNodeCount);
  PRIVATE(this)->pathIndices.resize(originalNodeCount);
  PRIVATE(this)->pathStatistics = originalStatistics;
  PRIVATE(this)->geometryPool.rewind(geometryCheckpoint);
  this->unsupportedRendering = originalUnsupported;
  this->unsupportedNode = originalUnsupportedNode;
  this->unsupportedReason = originalUnsupportedReason;
  return replacementsCompatible ? static_cast<int>(commandIndices.size()) : 0;
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
  PRIVATE(this)->lastPathInstanceId = 0;
  PRIVATE(this)->pathStatistics = PathStatistics();
  PRIVATE(this)->geometryRecipes.clear();
  PRIVATE(this)->geometryRecipeCache.fill(
    SoIRRenderActionP::GeometryRecipeCacheEntry());
  PRIVATE(this)->geometrySourceCache.fill(
    SoIRRenderActionP::GeometrySourceCacheEntry());
  PRIVATE(this)->pathStatistics.estimatedStorageBytes =
    PRIVATE(this)->ownedPathNodeTable.size() * sizeof(SoNode *);
}

void
SoIRRenderAction::clearCommandPaths()
{
  for (SoPath * path : this->commandPaths) {
    if (path && SoDB::isInitialized()) path->unref();
  }
  this->commandPaths.clear();
  for (SoNode * node : PRIVATE(this)->ownedPathNodes) {
    if (node && SoDB::isInitialized()) node->unref();
  }
  PRIVATE(this)->commandPathRecords.clear();
  PRIVATE(this)->pathNodes.clear();
  PRIVATE(this)->pathIndices.clear();
  PRIVATE(this)->ownedPathNodes.clear();
  PRIVATE(this)->resetDependencyHeads();
  PRIVATE(this)->branchDependencyLinks.clear();
  PRIVATE(this)->recordBranchDependencies = true;
  PRIVATE(this)->resetPathNodeTable();
  PRIVATE(this)->renderStage = SoRenderStage::Main;
  PRIVATE(this)->hasRenderContextOverride = false;
  PRIVATE(this)->renderContextOverride = SoIRRenderContext();
}
