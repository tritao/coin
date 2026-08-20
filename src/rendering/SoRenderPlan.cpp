
#include "rendering/SoRenderPlan.h"

#include <algorithm>

void
SoRenderPlanner::build(const SoDrawList & drawlist,
                       const SbMatrix & frameViewMatrix,
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
    uint8_t opaqueGroup = 2;
    uint64_t geometryKey = 0;
    SoLightingHandle lightingHandle = 0;
  };

  const auto depthOf = [&drawlist, &frameViewMatrix](const uint32_t commandIndex) {
    const SoRenderCommand & command = drawlist.getCommand(
      static_cast<int>(commandIndex));
    SbMat view;
    const SbMatrix & effectiveView = command.state.useCommandMatrices
      ? command.viewMatrix : frameViewMatrix;
    effectiveView.getValue(view);
    const SoGeometryDesc & geometry = drawlist.getCommandGeometry(command);
    SbVec3f worldCenter;
    command.modelMatrix.multVecMatrix(
      geometry.hasBounds ? geometry.boundsCenter : SbVec3f(0.0f, 0.0f, 0.0f),
      worldCenter);
    const float worldX = worldCenter[0];
    const float worldY = worldCenter[1];
    const float worldZ = worldCenter[2];
    const float eyeZ = view[0][2] * worldX +
      view[1][2] * worldY +
      view[2][2] * worldZ +
      view[3][2];
    return -eyeZ;
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
      const SoGeometryDesc & geometry =
        drawlist.getCommandGeometry(command);
      const SoTextureData & texture = command.material.texture;
      const bool textured = texture.cacheKey != 0 || texture.pixels != NULL;
      planned.geometryKey = geometry.resourceKey;
      planned.lightingHandle = command.lightingHandle;
      const bool groupableOpaque =
        planned.opacity == SO_OPACITY_OPAQUE && planned.geometryKey != 0 &&
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
      if (groupableOpaque &&
          geometry.topology == SO_TOPOLOGY_TRIANGLES) {
        planned.opaqueGroup = 0;
      }
      else if (groupableOpaque && geometry.topology == SO_TOPOLOGY_LINES &&
               command.state.raster.lineWidth <= 1.0f &&
               command.state.raster.linePattern == 0xFFFF) {
        planned.opaqueGroup = 1;
      }
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
    std::stable_sort(commands.begin(), commands.end(),
      [](const PlannedCommand & lhs, const PlannedCommand & rhs) {
        if (lhs.opaqueGroup != rhs.opaqueGroup) {
          return lhs.opaqueGroup < rhs.opaqueGroup;
        }
        if (lhs.opaqueGroup >= 2) return false;
        if (lhs.geometryKey != rhs.geometryKey) {
          return lhs.geometryKey < rhs.geometryKey;
        }
        return lhs.lightingHandle < rhs.lightingHandle;
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
