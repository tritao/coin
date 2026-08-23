
#include "rendering/SoRenderPlan.h"

#include <algorithm>

bool
SoRenderCommandTraits::sameMaterialUniformState(const SoMaterialData & lhs,
                                                const SoMaterialData & rhs)
{
  return lhs.diffuse == rhs.diffuse && lhs.ambient == rhs.ambient &&
    lhs.specular == rhs.specular && lhs.emissive == rhs.emissive &&
    lhs.shadingModel == rhs.shadingModel && lhs.shininess == rhs.shininess &&
    lhs.texture.cacheKey == rhs.texture.cacheKey &&
    lhs.texture.revision == rhs.texture.revision &&
    lhs.texture.model == rhs.texture.model &&
    lhs.texture.blendColor == rhs.texture.blendColor &&
    lhs.textureAlphaIncludesOpacity == rhs.textureAlphaIncludesOpacity &&
    lhs.vertexColorAlphaIncludesOpacity ==
      rhs.vertexColorAlphaIncludesOpacity &&
    lhs.twoSidedLighting == rhs.twoSidedLighting;
}

bool
SoRenderCommandTraits::sameInstancedMaterialState(
  const SoMaterialData & lhs, const SoMaterialData & rhs)
{
  return lhs.shadingModel == SO_SHADING_UNLIT &&
    rhs.shadingModel == SO_SHADING_UNLIT &&
    lhs.ambient == rhs.ambient && lhs.specular == rhs.specular &&
    lhs.emissive == rhs.emissive && lhs.shininess == rhs.shininess &&
    lhs.texture.cacheKey == rhs.texture.cacheKey &&
    lhs.texture.revision == rhs.texture.revision &&
    lhs.texture.model == rhs.texture.model &&
    lhs.texture.blendColor == rhs.texture.blendColor &&
    lhs.textureAlphaIncludesOpacity == rhs.textureAlphaIncludesOpacity &&
    lhs.vertexColorAlphaIncludesOpacity ==
      rhs.vertexColorAlphaIncludesOpacity &&
    lhs.twoSidedLighting == rhs.twoSidedLighting;
}

bool
SoRenderCommandTraits::sameTextureBinding(const SoTextureData & lhs,
                                          const SoTextureData & rhs)
{
  return lhs.cacheKey == rhs.cacheKey && lhs.revision == rhs.revision &&
    lhs.width == rhs.width && lhs.height == rhs.height &&
    lhs.numComponents == rhs.numComponents &&
    lhs.hasTransparency == rhs.hasTransparency &&
    lhs.minFilter == rhs.minFilter && lhs.magFilter == rhs.magFilter &&
    lhs.wrapS == rhs.wrapS && lhs.wrapT == rhs.wrapT &&
    lhs.anisotropic == rhs.anisotropic &&
    lhs.model == rhs.model && lhs.blendColor == rhs.blendColor;
}

bool
SoRenderCommandTraits::sameBlendState(const SoBlendState & lhs,
                                      const SoBlendState & rhs)
{
  return lhs.enabled == rhs.enabled &&
    lhs.srcRGBFactor == rhs.srcRGBFactor &&
    lhs.dstRGBFactor == rhs.dstRGBFactor &&
    lhs.srcAlphaFactor == rhs.srcAlphaFactor &&
    lhs.dstAlphaFactor == rhs.dstAlphaFactor &&
    lhs.rgbEquation == rhs.rgbEquation &&
    lhs.alphaEquation == rhs.alphaEquation;
}

SoRenderCommandTraits::OpaqueGroup
SoRenderCommandTraits::classifyOpaqueGroup(
  const SoRenderCommand & command, const SoGeometryDesc & geometry)
{
  const SoTextureData & texture = command.material.texture;
  const bool textured = texture.cacheKey != 0 || texture.pixels != nullptr;

  // Only ordinary opaque, depth-writing geometry is safe to reorder. Equal
  // depth results and viewport-local effects can make specialized raster
  // state depend on insertion order.
  const bool groupable = command.opacityClass == SO_OPACITY_OPAQUE &&
    geometry.cacheKey != 0 &&
    command.material.shadingModel == SO_SHADING_UNLIT && !textured &&
    command.material.opacity == 1.0f &&
    command.material.diffuse[3] == 1.0f &&
    !command.state.blend.enabled && command.state.depth.enabled &&
    command.state.depth.writeEnabled &&
    command.state.alphaTest.policy == SO_ALPHA_TEST_POLICY_NONE &&
    command.state.raster.visible &&
    command.state.raster.fillMode == SO_RASTER_FILL &&
    !command.state.raster.viewportOverride &&
    !command.state.raster.polygonOffsetFilled &&
    !command.state.raster.polygonOffsetLines &&
    !command.state.raster.polygonOffsetPoints &&
    !command.state.useCommandMatrices && !command.pixelRaster.enabled;
  if (!groupable) return OpaqueGroup::NONE;
  if (geometry.topology == SO_TOPOLOGY_TRIANGLES) {
    return OpaqueGroup::TRIANGLES;
  }
  if (geometry.topology == SO_TOPOLOGY_LINES &&
      command.state.raster.lineWidth <= 1.0f &&
      command.state.raster.linePattern == 0xFFFF) {
    return OpaqueGroup::NATIVE_LINES;
  }
  return OpaqueGroup::NONE;
}

SoRenderCommandTraits::PlanningClass
SoRenderCommandTraits::classifyPlanning(const SoRenderCommand & command)
{
  return command.opacityClass == SO_OPACITY_OPAQUE
    ? PlanningClass::OPAQUE_INSERTION_ORDER
    : PlanningClass::TRANSPARENT_DEPTH_SORTED;
}

bool
SoRenderCommandTraits::transformAffectsPlanning(
  const SoRenderCommand & command)
{
  return classifyPlanning(command) == PlanningClass::TRANSPARENT_DEPTH_SORTED;
}

void
SoRenderPlanner::build(const SoDrawList & drawlist,
                       SoRenderPlan & plan) const
{
  plan.operations.clear();
  const uint32_t commandCount = static_cast<uint32_t>(drawlist.getNumCommands());
  const std::vector<SoDepthClearEvent> & events = drawlist.getDepthClearEvents();
  plan.operations.reserve(commandCount + events.size() * 2 + 4);

  struct PlannedCommand {
    uint32_t commandIndex = 0;
    SoOpacityClass opacity = SO_OPACITY_OPAQUE;
    float depth = 0.0f;
  };

  const auto depthOf = [&drawlist](const uint32_t commandIndex) {
    const SoRenderCommand & command = drawlist.getCommand(
      static_cast<int>(commandIndex));
    const SoGeometryDesc & geometry = drawlist.getCommandGeometry(command);
    const SbVec3f localCenter = geometry.hasBounds
      ? geometry.boundsCenter : SbVec3f(0.0f, 0.0f, 0.0f);
    SbVec3f worldCenter;
    SbVec3f eyeCenter;
    command.modelMatrix.multVecMatrix(localCenter, worldCenter);
    command.viewMatrix.multVecMatrix(worldCenter, eyeCenter);
    return -eyeCenter[2];
  };

  const auto emitSegment = [&drawlist, &plan, &depthOf](
    const SoRenderStage stage, const uint32_t begin, const uint32_t end) {
    std::vector<PlannedCommand> commands;
    for (uint32_t commandIndex = begin; commandIndex < end; ++commandIndex) {
      const SoRenderCommand & command = drawlist.getCommand(
        static_cast<int>(commandIndex));
      if (command.stage != stage) continue;
      PlannedCommand planned;
      planned.commandIndex = commandIndex;
      planned.opacity = command.opacityClass;
      planned.depth = depthOf(commandIndex);
      commands.push_back(planned);
    }
    std::stable_sort(commands.begin(), commands.end(),
      [](const PlannedCommand & lhs, const PlannedCommand & rhs) {
        if (lhs.opacity != rhs.opacity) {
          return lhs.opacity == SO_OPACITY_OPAQUE;
        }
        if (lhs.opacity == SO_OPACITY_OPAQUE) return false;
        return lhs.depth > rhs.depth;
      });
    for (const PlannedCommand & command : commands) {
      SoRenderOperation draw;
      draw.type = SoRenderOperationType::DRAW;
      draw.commandIndex = command.commandIndex;
      plan.operations.push_back(draw);
    }
  };

  const SoRenderStage stages[] = {
    SoRenderStage::Background,
    SoRenderStage::Main,
    SoRenderStage::AfterMain,
    SoRenderStage::Foreground
  };
  for (const SoRenderStage stage : stages) {
    std::vector<size_t> stageEvents;
    for (size_t eventIndex = 0; eventIndex < events.size(); ++eventIndex) {
      if (events[eventIndex].stage == stage) stageEvents.push_back(eventIndex);
    }
    uint32_t begin = 0;
    for (size_t nextEvent = 0; nextEvent <= stageEvents.size();
         ++nextEvent) {
      const uint32_t end = nextEvent < stageEvents.size()
        ? std::min(events[stageEvents[nextEvent]].sequence, commandCount)
        : commandCount;
      emitSegment(stage, begin, end);
      if (nextEvent == stageEvents.size()) break;

      SoRenderOperation barrier;
      barrier.type = SoRenderOperationType::END_DEPTH_SEGMENT;
      plan.operations.push_back(barrier);

      SoRenderOperation clear;
      clear.type = SoRenderOperationType::CLEAR_DEPTH;
      clear.depthClearEventIndex = static_cast<uint32_t>(
        stageEvents[nextEvent]);
      plan.operations.push_back(clear);
      begin = end;
    }

    SoRenderOperation stageEnd;
    stageEnd.type = SoRenderOperationType::END_DEPTH_SEGMENT;
    plan.operations.push_back(stageEnd);
  }

  if (plan.operations.empty() ||
      plan.operations.back().type != SoRenderOperationType::END_DEPTH_SEGMENT) {
    SoRenderOperation frameEnd;
    frameEnd.type = SoRenderOperationType::END_DEPTH_SEGMENT;
    plan.operations.push_back(frameEnd);
  }
}
