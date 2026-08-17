
#include "rendering/SoRenderPlan.h"

#include <algorithm>

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
    const SbVec3f localCenter = command.geometry.hasBounds
      ? command.geometry.boundsCenter : SbVec3f(0.0f, 0.0f, 0.0f);
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
