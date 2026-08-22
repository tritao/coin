// src/rendering/SoRenderIR.cpp

#include "rendering/SoRenderIRP.h"
#include "rendering/SoTextureQualityPolicy.h"
#include "elements/SoLazyElementP.h"

#include <Inventor/C/tidbits.h>
#include <Inventor/elements/SoDevicePixelRatioElement.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoEnvironmentElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoLightAttenuationElement.h>
#include <Inventor/elements/SoLightElement.h>
#include <Inventor/elements/SoLightModelElement.h>
#include <Inventor/elements/SoLinePatternElement.h>
#include <Inventor/elements/SoLineWidthElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoMultiTextureImageElement.h>
#include <Inventor/elements/SoPointSizeElement.h>
#include <Inventor/elements/SoRenderMatrixPolicyElement.h>
#include <Inventor/elements/SoPickStyleElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoTextureQualityElement.h>
#include <Inventor/elements/SoShapeHintsElement.h>
#include <Inventor/elements/SoShapeStyleElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include "elements/SoRenderPlacementElement.h"
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoLight.h>
#include <Inventor/nodes/SoPointLight.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoSpotLight.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cstdlib>
#include <inttypes.h>

namespace {

bool
lightingEqual(const SoLightData & lhs, const SoLightData & rhs)
{
  return lhs.type == rhs.type &&
         lhs.color == rhs.color &&
         lhs.direction == rhs.direction &&
         lhs.position == rhs.position &&
         lhs.attenuation == rhs.attenuation &&
         lhs.spotCutoffCos == rhs.spotCutoffCos &&
         lhs.spotExponent == rhs.spotExponent;
}

bool
lightingEqual(const SoLightingData & lhs, const SoLightingData & rhs)
{
  if (lhs.ambient != rhs.ambient || lhs.lights.size() != rhs.lights.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.lights.size(); ++i) {
    if (!lightingEqual(lhs.lights[i], rhs.lights[i])) {
      return false;
    }
  }
  return true;
}

enum LegacyGLBlendFactorValue {
  LEGACY_GL_ZERO = 0x0000,
  LEGACY_GL_ONE = 0x0001,
  LEGACY_GL_SRC_COLOR = 0x0300,
  LEGACY_GL_ONE_MINUS_SRC_COLOR = 0x0301,
  LEGACY_GL_SRC_ALPHA = 0x0302,
  LEGACY_GL_ONE_MINUS_SRC_ALPHA = 0x0303,
  LEGACY_GL_DST_ALPHA = 0x0304,
  LEGACY_GL_ONE_MINUS_DST_ALPHA = 0x0305,
  LEGACY_GL_DST_COLOR = 0x0306,
  LEGACY_GL_ONE_MINUS_DST_COLOR = 0x0307,
  LEGACY_GL_SRC_ALPHA_SATURATE = 0x0308,
  LEGACY_GL_CONSTANT_COLOR = 0x8001,
  LEGACY_GL_ONE_MINUS_CONSTANT_COLOR = 0x8002,
  LEGACY_GL_CONSTANT_ALPHA = 0x8003,
  LEGACY_GL_ONE_MINUS_CONSTANT_ALPHA = 0x8004,
  LEGACY_GL_SRC1_ALPHA = 0x8589,
  LEGACY_GL_SRC1_COLOR = 0x88F9,
  LEGACY_GL_ONE_MINUS_SRC1_COLOR = 0x88FA,
  LEGACY_GL_ONE_MINUS_SRC1_ALPHA = 0x88FB
};

enum LegacyGLAlphaTestFunctionValue {
  LEGACY_GL_NEVER = 0x0200,
  LEGACY_GL_LESS = 0x0201,
  LEGACY_GL_EQUAL = 0x0202,
  LEGACY_GL_LEQUAL = 0x0203,
  LEGACY_GL_GREATER = 0x0204,
  LEGACY_GL_NOTEQUAL = 0x0205,
  LEGACY_GL_GEQUAL = 0x0206,
  LEGACY_GL_ALWAYS = 0x0207
};

SoBlendFactor
blendFactorFromLegacyGL(const int value)
{
  // Keep the legacy GL values local to this conversion boundary. No GL enum
  // is stored in the public IR.
  switch (value) {
  case LEGACY_GL_ZERO: return SO_BLEND_FACTOR_ZERO;
  case LEGACY_GL_ONE: return SO_BLEND_FACTOR_ONE;
  case LEGACY_GL_SRC_COLOR: return SO_BLEND_FACTOR_SRC_COLOR;
  case LEGACY_GL_ONE_MINUS_SRC_COLOR: return SO_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
  case LEGACY_GL_SRC_ALPHA: return SO_BLEND_FACTOR_SRC_ALPHA;
  case LEGACY_GL_ONE_MINUS_SRC_ALPHA: return SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  case LEGACY_GL_DST_ALPHA: return SO_BLEND_FACTOR_DST_ALPHA;
  case LEGACY_GL_ONE_MINUS_DST_ALPHA: return SO_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
  case LEGACY_GL_DST_COLOR: return SO_BLEND_FACTOR_DST_COLOR;
  case LEGACY_GL_ONE_MINUS_DST_COLOR: return SO_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
  case LEGACY_GL_SRC_ALPHA_SATURATE: return SO_BLEND_FACTOR_SRC_ALPHA_SATURATE;
  case LEGACY_GL_CONSTANT_COLOR: return SO_BLEND_FACTOR_CONSTANT_COLOR;
  case LEGACY_GL_ONE_MINUS_CONSTANT_COLOR: return SO_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
  case LEGACY_GL_CONSTANT_ALPHA: return SO_BLEND_FACTOR_CONSTANT_ALPHA;
  case LEGACY_GL_ONE_MINUS_CONSTANT_ALPHA: return SO_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
  // Dual-source factors are represented by the later material/lighting
  // layer. The backend-neutral base has no corresponding IR vocabulary yet,
  // so retain the historical primary-source approximation here.
  case LEGACY_GL_SRC1_ALPHA: return SO_BLEND_FACTOR_SRC_ALPHA;
  case LEGACY_GL_SRC1_COLOR: return SO_BLEND_FACTOR_SRC_COLOR;
  case LEGACY_GL_ONE_MINUS_SRC1_COLOR: return SO_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
  case LEGACY_GL_ONE_MINUS_SRC1_ALPHA: return SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  default:     return SO_BLEND_FACTOR_ONE;
  }
}

SoAlphaTestFunction
alphaTestFunctionFromLegacyGL(const int value)
{
  switch (value) {
  case LEGACY_GL_NEVER: return SO_ALPHA_TEST_NEVER;
  case LEGACY_GL_ALWAYS: return SO_ALPHA_TEST_ALWAYS;
  case LEGACY_GL_LESS: return SO_ALPHA_TEST_LESS;
  case LEGACY_GL_LEQUAL: return SO_ALPHA_TEST_LEQUAL;
  case LEGACY_GL_EQUAL: return SO_ALPHA_TEST_EQUAL;
  case LEGACY_GL_GEQUAL: return SO_ALPHA_TEST_GEQUAL;
  case LEGACY_GL_GREATER: return SO_ALPHA_TEST_GREATER;
  case LEGACY_GL_NOTEQUAL: return SO_ALPHA_TEST_NOTEQUAL;
  default:     return SO_ALPHA_TEST_NONE;
  }
}

SoTextureModel
textureModelFromLegacy(SoMultiTextureImageElement::Model model)
{
  switch (model) {
  case SoMultiTextureImageElement::DECAL:
    return SO_TEXTURE_MODEL_DECAL;
  case SoMultiTextureImageElement::BLEND:
    return SO_TEXTURE_MODEL_BLEND;
  case SoMultiTextureImageElement::REPLACE:
    return SO_TEXTURE_MODEL_REPLACE;
  case SoMultiTextureImageElement::MODULATE:
  default:
    return SO_TEXTURE_MODEL_MODULATE;
  }
}

void
textureFiltersFromQuality(const float quality, SoTextureData & texture)
{
  const CoinTextureQualityPolicy policy =
    coin_get_texture_quality_policy(quality);
  if (!policy.linear) {
    texture.minFilter = SO_TEXTURE_FILTER_NEAREST;
    texture.magFilter = SO_TEXTURE_FILTER_NEAREST;
  }
  else if (!policy.mipmap) {
    texture.minFilter = SO_TEXTURE_FILTER_LINEAR;
    texture.magFilter = SO_TEXTURE_FILTER_LINEAR;
  }
  else if (!policy.linearMipmap) {
    texture.minFilter = SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR;
    texture.magFilter = SO_TEXTURE_FILTER_LINEAR;
  }
  else {
    texture.minFilter = SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR;
    texture.magFilter = SO_TEXTURE_FILTER_LINEAR;
  }

  // Preserve the LegacyGL quality policy in the neutral IR. The GL
  // executor selects the active driver's supported anisotropy level.
  texture.anisotropic = policy.anisotropic;
}

} // namespace

SbBool
coin_render_ir_trace_enabled()
{
  static int initialized = 0;
  static SbBool enabled = FALSE;
  if (!initialized) {
    enabled = coin_getenv("COIN_DEBUG_RENDER_IR") ? TRUE : FALSE;
    initialized = 1;
  }
  return enabled;
}

SoIRBuffer::SoIRBuffer()
{
}

constexpr size_t SoIRBuffer::MIN_CHUNK_SIZE;

void
SoIRBuffer::clear()
{
  // Track high-water mark so we can pre-size on next frame
  if (this->totalAllocated > this->highWaterMark) {
    this->highWaterMark = this->totalAllocated;
  }
  // Reset cursors but keep chunks allocated
  for (auto & chunk : this->chunks) {
    chunk->cursor = 0;
  }
  this->totalAllocated = 0;
}

void
SoIRBuffer::reserve(size_t bytes)
{
  // Ensure the first chunk is at least this large
  if (this->chunks.empty()) {
    std::unique_ptr<Chunk> c(new Chunk);
    c->data.resize(std::max(bytes, MIN_CHUNK_SIZE));
    this->chunks.push_back(std::move(c));
  } else if (bytes > this->chunks[0]->data.size()) {
    // Only resize the first chunk if it hasn't been used yet
    if (this->chunks[0]->cursor == 0) {
      this->chunks[0]->data.resize(bytes);
    }
  }
}

void *
SoIRBuffer::allocate(size_t bytes, size_t alignment)
{
  if (alignment == 0) alignment = 1;

  // Try to allocate from an existing chunk
  for (auto & chunk : this->chunks) {
    size_t aligned = (chunk->cursor + alignment - 1) & ~(alignment - 1);
    if (aligned + bytes <= chunk->data.size()) {
      void * ptr = chunk->data.data() + aligned;
      chunk->cursor = aligned + bytes;
      this->totalAllocated += bytes;
      return ptr;
    }
  }

  // Need a new chunk — size it to at least fit this allocation
  // and to avoid many small chunks
  size_t chunkSize = std::max({bytes, MIN_CHUNK_SIZE, this->highWaterMark / 2});
  std::unique_ptr<Chunk> c(new Chunk);
  c->data.resize(chunkSize);
  c->cursor = bytes;
  void * ptr = c->data.data();
  this->chunks.push_back(std::move(c));
  this->totalAllocated += bytes;
  return ptr;
}

SoIRBuffer::Checkpoint
SoIRBuffer::checkpoint() const
{
  Checkpoint result;
  result.chunkCount = this->chunks.size();
  result.totalAllocated = this->totalAllocated;
  result.cursors.reserve(result.chunkCount);
  for (const auto & chunk : this->chunks) result.cursors.push_back(chunk->cursor);
  return result;
}

void
SoIRBuffer::rewind(const Checkpoint & checkpoint)
{
  this->chunks.resize(checkpoint.chunkCount);
  for (size_t i = 0; i < checkpoint.cursors.size(); ++i) {
    this->chunks[i]->cursor = checkpoint.cursors[i];
  }
  this->totalAllocated = checkpoint.totalAllocated;
}

SoDrawList::SoDrawList()
{
}

void
SoDrawList::clear()
{
  this->commands.clear();
  this->geometryResources.clear();
  this->lightingSetups.clear();
  this->depthClearEvents.clear();
  this->selection = SoSelectionState();
  this->pickLUT.clear();
  this->generation++;
  this->contentRevision++;
  this->renderPlanRevision++;
  this->resourceRevision++;
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
}

void
SoDrawList::truncate(int count)
{
  if (count < static_cast<int>(this->commands.size())) {
    ++this->contentRevision;
    ++this->renderPlanRevision;
    ++this->resourceRevision;
    this->commands.resize(static_cast<size_t>(count));
    auto trimTargets = [count](std::vector<SoSelectionTarget> & targets) {
      targets.erase(
        std::remove_if(
          targets.begin(), targets.end(),
          [count](const SoSelectionTarget & target) {
            return target.commandIndex >= count;
          }),
        targets.end());
    };
    trimTargets(this->selection.selected);
    trimTargets(this->selection.highlighted);
    while (!this->depthClearEvents.empty() &&
           this->depthClearEvents.back().sequence >
             static_cast<uint32_t>(count)) {
      this->depthClearEvents.pop_back();
    }
    this->pickLUT.clear();
    this->pickLUTGeneration = 0;
    this->pickLUTValid = false;
  }
}

void
SoDrawList::reserve(int count)
{
  this->commands.reserve(static_cast<size_t>(count));
}

void
SoDrawList::addCommand(const SoRenderCommand & cmd)
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  this->commands.push_back(cmd);
  this->pickLUT.clear();
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
}

SoRenderCommand &
SoDrawList::emplaceCommand()
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  this->pickLUT.clear();
  this->pickLUTGeneration = 0;
  this->commands.emplace_back();
  return this->commands.back();
}

SoGeometryHandle
SoDrawList::addGeometryResource(const SoGeometryResource & resource)
{
  this->geometryResources.push_back(resource);
  return static_cast<SoGeometryHandle>(this->geometryResources.size());
}

SoGeometryResource *
SoDrawList::getGeometryResource(SoGeometryHandle handle)
{
  if (handle == SO_INVALID_GEOMETRY_HANDLE ||
      static_cast<size_t>(handle) > this->geometryResources.size()) {
    return nullptr;
  }
  return &this->geometryResources[static_cast<size_t>(handle - 1)];
}

const SoGeometryResource *
SoDrawList::getGeometryResource(SoGeometryHandle handle) const
{
  if (handle == SO_INVALID_GEOMETRY_HANDLE ||
      static_cast<size_t>(handle) > this->geometryResources.size()) {
    return nullptr;
  }
  return &this->geometryResources[static_cast<size_t>(handle - 1)];
}

int
SoDrawList::getNumGeometryResources() const
{
  return static_cast<int>(this->geometryResources.size());
}

void
SoDrawList::truncateGeometryResources(int count)
{
  if (count >= 0 && count < static_cast<int>(this->geometryResources.size())) {
    ++this->contentRevision;
    ++this->renderPlanRevision;
    ++this->resourceRevision;
    this->geometryResources.resize(static_cast<size_t>(count));
  }
}

void
SoDrawList::markGeometryResourcesChanged()
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  this->pickLUT.clear();
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
}

SoRenderCommand &
SoDrawList::getCommandForRetainedUpdate(int i)
{
  return this->commands[static_cast<size_t>(i)];
}

SoDrawList::MutationCheckpoint
SoDrawList::createMutationCheckpoint() const
{
  MutationCheckpoint checkpoint;
  checkpoint.commandCount = static_cast<int>(this->commands.size());
  checkpoint.geometryResourceCount =
    static_cast<int>(this->geometryResources.size());
  checkpoint.lightingSetupCount = static_cast<int>(this->lightingSetups.size());
  checkpoint.depthEventCount = static_cast<int>(this->depthClearEvents.size());
  checkpoint.selection = this->selection;
  checkpoint.pickLUT = this->pickLUT;
  checkpoint.generation = this->generation;
  checkpoint.contentRevision = this->contentRevision;
  checkpoint.renderPlanRevision = this->renderPlanRevision;
  checkpoint.resourceRevision = this->resourceRevision;
  checkpoint.pickLUTGeneration = this->pickLUTGeneration;
  checkpoint.pickLUTRevision = this->pickLUTRevision;
  checkpoint.pickLUTValid = this->pickLUTValid;
  return checkpoint;
}

void
SoDrawList::restoreMutationCheckpoint(const MutationCheckpoint & checkpoint)
{
  this->commands.resize(static_cast<size_t>(checkpoint.commandCount));
  this->geometryResources.resize(
    static_cast<size_t>(checkpoint.geometryResourceCount));
  this->lightingSetups.resize(
    static_cast<size_t>(checkpoint.lightingSetupCount));
  this->depthClearEvents.resize(static_cast<size_t>(checkpoint.depthEventCount));
  this->selection = checkpoint.selection;
  this->pickLUT = checkpoint.pickLUT;
  this->generation = checkpoint.generation;
  this->contentRevision = checkpoint.contentRevision;
  this->renderPlanRevision = checkpoint.renderPlanRevision;
  this->resourceRevision = checkpoint.resourceRevision;
  this->pickLUTGeneration = checkpoint.pickLUTGeneration;
  this->pickLUTRevision = checkpoint.pickLUTRevision;
  this->pickLUTValid = checkpoint.pickLUTValid;
}

void
SoDrawList::applyRetainedInvalidation(const bool plan, const bool resources,
                                      const bool pickTopology)
{
  ++this->contentRevision;
  if (plan) ++this->renderPlanRevision;
  if (resources) ++this->resourceRevision;
  if (pickTopology) {
    this->pickLUT.clear();
    this->pickLUTGeneration = 0;
    this->pickLUTValid = false;
  }
}

const SoGeometryDesc &
SoDrawList::getCommandGeometry(const SoRenderCommand & command) const
{
  const SoGeometryResource * resource =
    this->getGeometryResource(command.geometryHandle);
  return resource ? resource->geometry : command.geometry;
}

const std::vector<SoRenderElementRange> &
SoDrawList::getCommandElementRanges(const SoRenderCommand & command) const
{
  if (command.pick.useResourceElementRanges) {
    const SoGeometryResource * resource =
      this->getGeometryResource(command.geometryHandle);
    if (resource) return resource->elementRanges;
  }
  return command.pick.elementRanges;
}

void
SoDrawList::addDepthClearEvent(const SoDepthClearEvent & event)
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  SoDepthClearEvent recorded = event;
  recorded.sequence = std::min(recorded.sequence,
                               static_cast<uint32_t>(this->commands.size()));
  this->depthClearEvents.push_back(recorded);
}

int
SoDrawList::getNumCommands() const
{
  return static_cast<int>(this->commands.size());
}

SoRenderCommand &
SoDrawList::getCommand(int i)
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  this->pickLUT.clear();
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
  return this->commands[static_cast<size_t>(i)];
}

const SoRenderCommand &
SoDrawList::getCommand(int i) const
{
  return this->commands[static_cast<size_t>(i)];
}

SoLightingHandle
SoDrawList::addLightingSetup(const SoLightingData & lighting)
{
  for (size_t i = 0; i < this->lightingSetups.size(); ++i) {
    if (lightingEqual(this->lightingSetups[i], lighting)) {
      return static_cast<SoLightingHandle>(i + 1);
    }
  }
  this->lightingSetups.push_back(lighting);
  return static_cast<SoLightingHandle>(this->lightingSetups.size());
}

const SoLightingData *
SoDrawList::getLighting(SoLightingHandle handle) const
{
  if (handle == 0) {
    return nullptr;
  }
  const size_t index = static_cast<size_t>(handle - 1);
  if (index >= this->lightingSetups.size()) {
    return nullptr;
  }
  return &this->lightingSetups[index];
}

void
SoDrawList::buildPickLUT() const
{
  if (this->pickLUTValid) return;
  this->pickLUT.clear();
  this->pickLUTGeneration = this->generation;
  ++this->pickLUTRevision;
  this->pickLUTValid = true;

  for (int commandIndex = 0; commandIndex < this->getNumCommands(); ++commandIndex) {
    const SoRenderCommand & command = this->getCommand(commandIndex);
    if (!command.pick.pickable || !command.state.raster.visible) continue;

    const bool indexed = command.geometry.indices != nullptr &&
      command.geometry.indexCount != 0;
    const uint32_t drawLimit = indexed ? command.geometry.indexCount
                                       : command.geometry.vertexCount;
    if (drawLimit == 0) continue;

    const std::vector<SoRenderElementRange> & elementRanges =
      this->getCommandElementRanges(command);
    if (elementRanges.empty()) {
      SoPickLUTEntry entry;
      entry.commandIndex = commandIndex;
      entry.objectId = command.objectId;
      entry.type = SO_PICK_OBJECT;
      entry.elementIndex = -1;
      entry.drawStart = 0;
      entry.drawCount = drawLimit;
      this->pickLUT.push_back(entry);
      continue;
    }

    for (const SoRenderElementRange & range : elementRanges) {
      SoPickLUTEntry entry;
      entry.commandIndex = commandIndex;
      entry.objectId = command.objectId;
      entry.type = range.type;
      entry.elementIndex = range.elementIndex;
      if (range.drawCount == 0 || range.drawStart >= drawLimit ||
          range.drawCount > drawLimit - range.drawStart) {
        continue;
      }
      entry.drawStart = range.drawStart;
      entry.drawCount = range.drawCount;
      this->pickLUT.push_back(entry);
    }
  }
}

const SoPickLUTEntry *
SoDrawList::resolvePickId(uint32_t id) const
{
  if (id == 0 || id > this->pickLUT.size()) return nullptr;
  if (this->pickLUTGeneration != this->generation) return nullptr;
  return &this->pickLUT[static_cast<size_t>(id - 1)];
}

SoRenderCommand *
SoDrawList::begin()
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  this->pickLUT.clear();
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
  return this->commands.empty() ? nullptr : this->commands.data();
}

SoRenderCommand *
SoDrawList::end()
{
  ++this->contentRevision;
  ++this->renderPlanRevision;
  ++this->resourceRevision;
  this->pickLUT.clear();
  this->pickLUTGeneration = 0;
  this->pickLUTValid = false;
  return this->commands.empty() ? nullptr : this->commands.data() + this->commands.size();
}

const SoRenderCommand *
SoDrawList::begin() const
{
  return this->commands.empty() ? nullptr : this->commands.data();
}

const SoRenderCommand *
SoDrawList::end() const
{
  return this->commands.empty() ? nullptr : this->commands.data() + this->commands.size();
}

static const char *
renderstage_name(SoRenderStage stage)
{
  switch (stage) {
  case SoRenderStage::Background: return "background";
  case SoRenderStage::Main: return "main";
  case SoRenderStage::AfterMain: return "after-main";
  case SoRenderStage::Foreground: return "foreground";
  default: return "unknown";
  }
}

void
SoIRDumpSummary(const SoDrawList & drawlist)
{
  if (!coin_render_ir_trace_enabled()) {
    return;
  }

  uint32_t minVerts = UINT32_MAX;
  uint32_t maxVerts = 0;
  const int num = drawlist.getNumCommands();
  for (int i = 0; i < num; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommand(i);
    const uint32_t vc = cmd.geometry.vertexCount;
    minVerts = std::min(minVerts, vc);
    maxVerts = std::max(maxVerts, vc);
  }

  SoDebugError::postInfo("SoDrawList",
                         "commands=%d minVerts=%u maxVerts=%u",
                         num,
                         minVerts == UINT32_MAX ? 0 : minVerts,
                         maxVerts);
}

void
SoIRDumpFirstN(const SoDrawList & drawlist, int count)
{
  if (!coin_render_ir_trace_enabled()) {
    return;
  }

  const int num = drawlist.getNumCommands();
  const int limit = std::min(num, count);
  for (int i = 0; i < limit; ++i) {
    const SoRenderCommand & cmd = drawlist.getCommand(i);
    const SbVec4f & diffuse = cmd.material.diffuse;
    const SoLightingData * lighting = drawlist.getLighting(cmd.lightingHandle);
    int numlights = lighting ? static_cast<int>(lighting->lights.size()) : -1;
    SbVec3f ambient(0.0f, 0.0f, 0.0f);
    if (lighting) {
      ambient = lighting->ambient;
    }
    SoDebugError::postInfo("SoDrawList",
                           "[%d] stage=%s depth=%d topo=%d verts=%u idx=%u colors=%p diffuse=(%.3f, %.3f, %.3f, %.3f) lights=%d ambient=(%.3f, %.3f, %.3f)",
                           i,
                           renderstage_name(cmd.stage),
                           cmd.state.depth.enabled,
                           static_cast<int>(cmd.geometry.topology),
                           cmd.geometry.vertexCount,
                           cmd.geometry.indexCount,
                           cmd.geometry.colors,
                           diffuse[0],
                           diffuse[1],
                           diffuse[2],
                           diffuse[3],
                           numlights,
                           ambient[0],
                           ambient[1],
                           ambient[2]);
  }
}

void
SoIRRenderContext::captureFromState(SoState * state)
{
  *this = SoIRRenderContext();
  if (!state) return;

  if (state->isElementEnabled(SoEnvironmentElement::getClassStackIndex()) &&
      state->isElementEnabled(SoLightAttenuationElement::getClassStackIndex()) &&
      state->isElementEnabled(SoLightElement::getClassStackIndex())) {
    SoRenderIR::captureLightingFromState(state, this->lighting);
    this->hasLighting = TRUE;
  }

  if (state->isElementEnabled(SoViewportRegionElement::getClassStackIndex())) {
    this->viewport = SoViewportRegionElement::get(state);
    this->hasViewport = TRUE;
  }
  if (state->isElementEnabled(SoModelMatrixElement::getClassStackIndex())) {
    this->modelMatrix = SoModelMatrixElement::get(state);
    this->hasModelMatrix = TRUE;
  }
  if (state->isElementEnabled(SoViewVolumeElement::getClassStackIndex())) {
    this->viewVolume = SoViewVolumeElement::get(state);
    this->hasViewVolume = TRUE;
  }
  if (state->isElementEnabled(SoViewingMatrixElement::getClassStackIndex())) {
    this->viewingMatrix = SoViewingMatrixElement::get(state);
    this->hasViewingMatrix = TRUE;
  }
  if (state->isElementEnabled(SoProjectionMatrixElement::getClassStackIndex())) {
    this->projectionMatrix = SoProjectionMatrixElement::get(state);
    this->hasProjectionMatrix = TRUE;
  }
  if (state->isElementEnabled(SoDevicePixelRatioElement::getClassStackIndex())) {
    this->devicePixelRatio = SoDevicePixelRatioElement::get(state);
    this->hasDevicePixelRatio = TRUE;
  }
}

void
SoIRRenderContext::applyToState(SoState * state, SbBool applyModelMatrix) const
{
  if (!state) return;

  if (applyModelMatrix && this->hasModelMatrix) {
    SoModelMatrixElement::set(state, nullptr, this->modelMatrix);
  }
  if (this->hasViewport) {
    SoViewportRegionElement::set(state, this->viewport);
  }
  if (this->hasViewVolume) {
    SoViewVolumeElement::set(state, nullptr, this->viewVolume);
  }
  if (this->hasViewingMatrix) {
    SoViewingMatrixElement::set(state, nullptr, this->viewingMatrix);
  }
  if (this->hasProjectionMatrix) {
    SoProjectionMatrixElement::set(state, nullptr, this->projectionMatrix);
  }
  if (this->hasDevicePixelRatio) {
    SoDevicePixelRatioElement::set(state, this->devicePixelRatio);
  }
}

namespace SoRenderIR {

static void fillTextureFromState(SoState * state, SoIRRenderAction * action,
                                 SoMaterialData & material);

static SoTextureWrap
textureWrapFromLegacy(SoMultiTextureImageElement::Wrap wrap)
{
  switch (wrap) {
  case SoMultiTextureImageElement::REPEAT:
    return SO_TEXTURE_WRAP_REPEAT;
  case SoMultiTextureImageElement::CLAMP_TO_BORDER:
    return SO_TEXTURE_WRAP_CLAMP_TO_BORDER;
  case SoMultiTextureImageElement::CLAMP:
  default:
    // GL_CLAMP is the historical Coin spelling for edge clamping here.
    return SO_TEXTURE_WRAP_CLAMP_TO_EDGE;
  }
}

static bool
hasTexture(const SoMaterialData & material)
{
  return material.texture.pixels != nullptr &&
    material.texture.width > 0 && material.texture.height > 0 &&
    material.texture.numComponents > 0;
}

static bool
textureHasTransparency(const SoTextureData & texture)
{
  // DECAL uses texture alpha as a color interpolation factor; it does not
  // change the fragment alpha that controls coverage or blending.
  if (texture.model == SO_TEXTURE_MODEL_DECAL) return false;
  if (texture.hasTransparency) return true;
  if (!texture.pixels || texture.width <= 0 || texture.height <= 0 ||
      (texture.numComponents != 2 && texture.numComponents != 4)) {
    return false;
  }
  const size_t pixelCount = static_cast<size_t>(texture.width) *
                            static_cast<size_t>(texture.height);
  for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
    const size_t alpha = pixel * static_cast<size_t>(texture.numComponents) +
                         static_cast<size_t>(texture.numComponents - 1);
    if (texture.pixels[alpha] != 0xffu) return true;
  }
  return false;
}

void
fillCommandTraversalStateFromAction(SoIRRenderAction * action,
                                    SoRenderCommand & command)
{
  SoState * state = action->getState();
  SoDrawList & drawlist = action->getMutableDrawList();
  command.modelMatrix = SoModelMatrixElement::get(state);
  command.viewMatrix = SoViewingMatrixElement::get(state);
  command.projMatrix = SoProjectionMatrixElement::get(state);
  fillRenderStateFromState(state, command.state);
  command.lightingHandle = fillLightingFromState(state, drawlist);
  command.pick.pickable = SoPickStyleElement::get(state) !=
    SoPickStyleElement::UNPICKABLE;
}

void
fillCommandStateFromAction(SoIRRenderAction * action,
                           SoRenderCommand & command,
                           const int materialIndex)
{
  SoState * state = action->getState();
  fillCommandTraversalStateFromAction(action, command);
  fillMaterialFromState(state, command.material, materialIndex);
  fillTextureFromState(state, action, command.material);
}

void
fillMaterialFromState(SoState * state, SoMaterialData & material,
                      int materialIndex)
{
  SoState * mutableState = state;
  const SbColor & diffuse = SoLazyElement::getDiffuse(mutableState, materialIndex);
  const SbColor & ambient = SoLazyElement::getAmbient(mutableState);
  const SbColor & specular = SoLazyElement::getSpecular(mutableState);
  const SbColor & emissive = SoLazyElement::getEmissive(mutableState);
  const float transparency = SoLazyElement::getTransparency(mutableState, materialIndex);

  // Keep diffuse and emissive independent. The explicit lighting shader owns
  // emissive contribution, so inferring diffuse from a default-looking
  // material would double-count emissive-only materials.
  material.diffuse.setValue(diffuse[0], diffuse[1], diffuse[2],
                            1.0f - transparency);

  // Capture the effective shading contract explicitly. Coin's traditional
  // PHONG light model currently maps to the legacy-compatible Gouraud path;
  // a true per-fragment PHONG path can be introduced without changing the
  // material/light payload carried by the IR.
  const int lightModel = SoLightModelElement::get(mutableState);
  const bool baseColor = lightModel == SoLightModelElement::BASE_COLOR;
  material.shadingModel = baseColor
    ? SO_SHADING_UNLIT
    : SO_SHADING_LEGACY_GOURAUD;
  material.twoSidedLighting = SoLazyElement::getTwoSidedLighting(mutableState) != FALSE;
  material.ambient.setValue(ambient[0], ambient[1], ambient[2], 1.0f);
  material.specular.setValue(specular[0], specular[1], specular[2], 1.0f);
  material.emissive.setValue(emissive[0], emissive[1], emissive[2], 1.0f);
  material.shininess = SoLazyElement::getShininess(mutableState);
  material.opacity = 1.0f - transparency;

  material.texture = SoTextureData();
  material.textureAlphaIncludesOpacity = false;
  material.vertexColorAlphaIncludesOpacity = false;
}

static void
fillTextureFromState(SoState * state, SoIRRenderAction * action,
                     SoMaterialData & material)
{
  if (!state || !action || !SoMultiTextureEnabledElement::get(state, 0)) {
    return;
  }

  SbVec2s size;
  int numComponents = 0;
  SoMultiTextureImageElement::Wrap wrapS;
  SoMultiTextureImageElement::Wrap wrapT;
  SoMultiTextureImageElement::Model model;
  SbColor blendColor;
  const unsigned char * bytes = SoMultiTextureImageElement::get(
    state, 0, size, numComponents, wrapS, wrapT, model, blendColor);
  if (!bytes || size[0] <= 0 || size[1] <= 0 ||
      numComponents < 1 || numComponents > 4) {
    return;
  }

  const size_t pixelCount = static_cast<size_t>(size[0]) *
                            static_cast<size_t>(size[1]);
  const size_t byteCount = pixelCount * static_cast<size_t>(numComponents);
  bool hasTransparency = false;
  const unsigned char * copy = action->allocateTextureStorage(
    bytes, byteCount, size[0], size[1], numComponents, hasTransparency);

  material.texture.pixels = copy;
  // The image element owns the source bytes and changes its node id whenever
  // the producing texture node changes. Preserve both pieces so backends can
  // retain one upload even though the IR owns a separate frame-local copy.
  material.texture.cacheKey = static_cast<uint64_t>(
    reinterpret_cast<uintptr_t>(bytes));
  material.texture.revision = static_cast<uint64_t>(
    SoMultiTextureImageElement::getNodeId(state, 0));
  material.texture.width = size[0];
  material.texture.height = size[1];
  material.texture.numComponents = numComponents;
  material.texture.hasTransparency = hasTransparency;
  material.texture.wrapS = textureWrapFromLegacy(wrapS);
  material.texture.wrapT = textureWrapFromLegacy(wrapT);
  material.texture.model = textureModelFromLegacy(model);
  material.texture.blendColor.setValue(blendColor[0], blendColor[1],
                                       blendColor[2], 1.0f);
  textureFiltersFromQuality(SoTextureQualityElement::get(state),
                            material.texture);
}

void
fillRenderStateFromState(SoState * state, SoRenderState & rs)
{
  SoState * mutableState = state;
  rs.useCommandMatrices =
    SoRenderMatrixPolicyElement::get(mutableState) ==
      SoRenderMatrixPolicyElement::CAPTURE_CURRENT_MATRICES;
  SbBool depthtest = TRUE;
  SbBool depthwrite = TRUE;
  SoDepthBufferElement::DepthWriteFunction depthfunc =
    SoDepthBufferElement::LEQUAL;
  SbVec2f range;
  SoDepthBufferElement::get(mutableState, depthtest, depthwrite, depthfunc, range);

  rs.depth.enabled = depthtest;
  rs.depth.writeEnabled = depthwrite;
  rs.depth.func = static_cast<SoDepthFunction>(depthfunc);
  rs.depth.range = range;

  int srcfactor = 0;
  int dstfactor = 0;
  rs.blend.enabled = SoLazyElement::getBlending(mutableState, srcfactor, dstfactor);
  rs.blend.srcRGBFactor = blendFactorFromLegacyGL(srcfactor);
  rs.blend.dstRGBFactor = blendFactorFromLegacyGL(dstfactor);

  // Ordinary LegacyGL blending applies the RGB factors to alpha as well.
  // Only explicit separate-alpha state supplies different alpha factors.
  int srcAlphaFactor = 0;
  int dstAlphaFactor = 0;
  if (SoLazyElement::getAlphaBlending(mutableState,
                                      srcAlphaFactor, dstAlphaFactor)) {
    rs.blend.srcAlphaFactor = blendFactorFromLegacyGL(srcAlphaFactor);
    rs.blend.dstAlphaFactor = blendFactorFromLegacyGL(dstAlphaFactor);
  } else {
    rs.blend.srcAlphaFactor = rs.blend.srcRGBFactor;
    rs.blend.dstAlphaFactor = rs.blend.dstRGBFactor;
  }


  // LegacyGL does not expose a Coin state element for blend equations. ADD
  // is its effective equation and is the only value that can be captured
  // deterministically from traversal.
  rs.blend.rgbEquation = SO_BLEND_EQUATION_ADD;
  rs.blend.alphaEquation = SO_BLEND_EQUATION_ADD;

  float alphaTestValue = 0.5f;
  const int alphaTestFunction = SoLazyElementP::getAlphaTestSemantic(
    mutableState, alphaTestValue);
  rs.alphaTest.function = static_cast<SoAlphaTestFunction>(alphaTestFunction);
  rs.alphaTest.reference = alphaTestValue;
  rs.alphaTest.policy = rs.alphaTest.function == SO_ALPHA_TEST_NONE
    ? SO_ALPHA_TEST_POLICY_NONE
    : SO_ALPHA_TEST_POLICY_EXPLICIT;

  SoDrawStyleElement::Style style = SoDrawStyleElement::get(mutableState);
  const SoShapeStyleElement * shapeStyle = SoShapeStyleElement::get(mutableState);
  rs.raster.visible = style != SoDrawStyleElement::INVISIBLE &&
    (!shapeStyle || !(shapeStyle->getFlags() & SoShapeStyleElement::INVISIBLE));
  SoRasterFillMode fillmode = SO_RASTER_FILL;
  switch (style) {
  case SoDrawStyleElement::LINES:
    fillmode = SO_RASTER_LINES;
    break;
  case SoDrawStyleElement::POINTS:
    fillmode = SO_RASTER_POINTS;
    break;
  default:
    fillmode = SO_RASTER_FILL;
    break;
  }
  rs.raster.fillMode = fillmode;
  // Native GL_POINTS are square unless point smoothing is enabled. Keep the
  // primitive shape explicit in the IR so backends do not choose independently.

  // Backface culling from SoShapeHintsElement.  Vertex ordering selects the
  // front-face winding; a solid shape requests back-face culling regardless
  // of whether its front faces are clockwise or counter-clockwise.
  {
    SoShapeHintsElement::VertexOrdering vo;
    SoShapeHintsElement::ShapeType st;
    SoShapeHintsElement::FaceType ft;
    SoShapeHintsElement::get(mutableState, vo, st, ft);
    rs.raster.cullBackFaces = st == SoShapeHintsElement::SOLID &&
      (vo == SoShapeHintsElement::CLOCKWISE ||
       vo == SoShapeHintsElement::COUNTERCLOCKWISE);
    rs.raster.frontFaceCCW = vo != SoShapeHintsElement::CLOCKWISE;
  }
  rs.raster.scissorEnabled = FALSE;
  rs.raster.lineWidth = SoLineWidthElement::get(mutableState);
  rs.raster.pointSize = SoPointSizeElement::get(mutableState);
  rs.raster.linePattern = static_cast<uint16_t>(
    SoLinePatternElement::get(mutableState));
  rs.raster.linePatternScale = static_cast<int16_t>(std::max(
    1, SoLinePatternElement::getScaleFactor(mutableState)));

  int viewportX = 0;
  int viewportY = 0;
  int viewportWidth = 0;
  int viewportHeight = 0;
  if (SoRenderPlacementElement::getViewport(mutableState,
                                            viewportX, viewportY,
                                            viewportWidth, viewportHeight)) {
    rs.raster.viewportOverride = TRUE;
    rs.raster.viewportEnabled = viewportWidth > 0 && viewportHeight > 0;
    rs.raster.viewportX = viewportX;
    rs.raster.viewportY = viewportY;
    rs.raster.viewportWidth = viewportWidth;
    rs.raster.viewportHeight = viewportHeight;
  }
  else {
    rs.raster.viewportOverride = FALSE;
    const SbViewportRegion & viewport = SoViewportRegionElement::get(mutableState);
    const SbVec2s & viewportOrigin = viewport.getViewportOriginPixels();
    const SbVec2s & viewportSize = viewport.getViewportSizePixels();
    rs.raster.viewportEnabled = viewportSize[0] > 0 && viewportSize[1] > 0;
    rs.raster.viewportX = viewportOrigin[0];
    rs.raster.viewportY = viewportOrigin[1];
    rs.raster.viewportWidth = viewportSize[0];
    rs.raster.viewportHeight = viewportSize[1];
  }

  float offsetfactor = 0.0f;
  float offsetunits = 0.0f;
  SoPolygonOffsetElement::Style offsetstyle = SoPolygonOffsetElement::FILLED;
  SbBool offseton = FALSE;
  SoPolygonOffsetElement::get(mutableState, offsetfactor, offsetunits,
                              offsetstyle, offseton);
  if (!offseton) {
    offsetfactor = 0.0f;
    offsetunits = 0.0f;
  }
  rs.raster.polygonOffsetFactor = offsetfactor;
  rs.raster.polygonOffsetUnits = offsetunits;
  rs.raster.polygonOffsetFilled = offseton &&
    (offsetstyle & SoPolygonOffsetElement::FILLED);
  rs.raster.polygonOffsetLines = offseton &&
    (offsetstyle & SoPolygonOffsetElement::LINES);
  rs.raster.polygonOffsetPoints = offseton &&
    (offsetstyle & SoPolygonOffsetElement::POINTS);
}

void
captureLightingFromState(SoState * state, SoLightingData & lighting)
{
  const SbColor & ambientColor = SoEnvironmentElement::getAmbientColor(state);
  const float ambientIntensity = SoEnvironmentElement::getAmbientIntensity(state);
  lighting.ambient.setValue(ambientColor[0] * ambientIntensity,
                            ambientColor[1] * ambientIntensity,
                            ambientColor[2] * ambientIntensity);

  const SbVec3f & attenuation = SoLightAttenuationElement::get(state);
  const SoNodeList & lights = SoLightElement::getLights(state);
  const int numLights = lights.getLength();
  lighting.lights.reserve(numLights);

  for (int i = 0; i < numLights; ++i) {
    SoLight * light = static_cast<SoLight *>(lights[i]);
    if (!light || !light->on.getValue()) {
      continue;
    }

    const SbColor lightColor = light->color.getValue();
    SoLightData lightData;
    lightData.color.setValue(lightColor[0] * light->intensity.getValue(),
                             lightColor[1] * light->intensity.getValue(),
                             lightColor[2] * light->intensity.getValue());

    const SbMatrix & lightMatrix = SoLightElement::getMatrix(state, i);

    if (light->isOfType(SoDirectionalLight::getClassTypeId())) {
      SoDirectionalLight * directional = static_cast<SoDirectionalLight *>(light);
      lightData.type = SO_LIGHT_DIRECTIONAL;
      lightMatrix.multDirMatrix(-(directional->direction.getValue()), lightData.direction);
      if (lightData.direction.normalize() == 0.0f) {
        lightData.direction.setValue(0.0f, 0.0f, 1.0f);
      }
    }
    else if (light->isOfType(SoPointLight::getClassTypeId())) {
      SoPointLight * point = static_cast<SoPointLight *>(light);
      lightData.type = SO_LIGHT_POINT;
      lightData.attenuation = attenuation;
      lightMatrix.multVecMatrix(point->location.getValue(), lightData.position);
    }
    else if (light->isOfType(SoSpotLight::getClassTypeId())) {
      SoSpotLight * spot = static_cast<SoSpotLight *>(light);
      lightData.type = SO_LIGHT_SPOT;
      lightData.attenuation = attenuation;
      lightMatrix.multVecMatrix(spot->location.getValue(), lightData.position);
      lightMatrix.multDirMatrix(spot->direction.getValue(), lightData.direction);
      if (lightData.direction.normalize() == 0.0f) {
        lightData.direction.setValue(0.0f, 0.0f, -1.0f);
      }
      float cutoff = spot->cutOffAngle.getValue();
      if (cutoff < 0.0f) cutoff = 0.0f;
      if (cutoff > float(M_PI) * 0.5f) cutoff = float(M_PI) * 0.5f;
      lightData.spotCutoffCos = std::cos(cutoff);
      float dropoff = spot->dropOffRate.getValue();
      if (dropoff < 0.0f) dropoff = 0.0f;
      if (dropoff > 1.0f) dropoff = 1.0f;
      lightData.spotExponent = dropoff * 128.0f;
    }
    else {
      continue;
    }

    lighting.lights.push_back(lightData);
  }

}

void
captureRenderContextFromState(SoState * state, SoIRRenderContext & context)
{
  context.captureFromState(state);
}

void
applyRenderContextToState(SoState * state, const SoIRRenderContext & context)
{
  context.applyToState(state);
}

SoLightingHandle
fillLightingFromState(SoState * state, SoDrawList & drawlist)
{
  SoLightingData lighting;
  captureLightingFromState(state, lighting);
  return drawlist.addLightingSetup(lighting);
}

SoLightingHandle
fillLightingFromState(SoState * /* state */,
                      SoDrawList & drawlist,
                      const SoLightingData & lighting)
{
  return drawlist.addLightingSetup(lighting);
}

bool
isMaterialTransparent(const SoMaterialData & material)
{
  return material.opacity < 0.999f || textureHasTransparency(material.texture);
}

void
ensureMaterialBlendState(SoRenderState & renderState,
                         const SoMaterialData & material)
{
  // SoIRRenderAction captures Coin's logical material state, while the
  // legacy GL action enables the conventional blend function as part of its
  // transparency setup. Make that implicit IR contract explicit without
  // replacing an actual non-standard blend state.
  if (renderState.blend.enabled || !isMaterialTransparent(material)) {
    return;
  }

  renderState.blend.enabled = TRUE;
  renderState.blend.srcRGBFactor = SO_BLEND_FACTOR_SRC_ALPHA;
  renderState.blend.dstRGBFactor = SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  renderState.blend.srcAlphaFactor = SO_BLEND_FACTOR_SRC_ALPHA;
  renderState.blend.dstAlphaFactor = SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  renderState.blend.rgbEquation = SO_BLEND_EQUATION_ADD;
  renderState.blend.alphaEquation = SO_BLEND_EQUATION_ADD;
}

void
finalizeCommand(SoRenderCommand & command)
{
  ensureMaterialBlendState(command.state, command.material);
  bool transparent = isMaterialTransparent(command.material);
  if (!transparent && command.geometry.colors) {
    for (uint32_t i = 0; i < command.geometry.vertexCount; ++i) {
      if (command.geometry.colors[i * 4 + 3] < 0.999f) {
        transparent = true;
        break;
      }
    }
  }
  command.opacityClass = transparent
    ? SO_OPACITY_TRANSPARENT : SO_OPACITY_OPAQUE;
  if (transparent && !command.state.blend.enabled) {
    command.state.blend.enabled = TRUE;
    command.state.blend.srcRGBFactor = SO_BLEND_FACTOR_SRC_ALPHA;
    command.state.blend.dstRGBFactor = SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    command.state.blend.srcAlphaFactor = SO_BLEND_FACTOR_SRC_ALPHA;
    command.state.blend.dstAlphaFactor = SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  }
}

} // namespace SoRenderIR
