
#include "rendering/SoRenderPlan.h"

#include <algorithm>

void
SoRenderPlanner::build(const SoDrawList & drawlist,
                       const SbMatrix & frameViewMatrix,
                       SoRenderPlan & plan) const
{
  struct PlannedEntry {
    SoPlannedDraw draw;
    SoOpacityClass opacity = SO_OPACITY_OPAQUE;
    float depth = 0.0f;
  };

  std::vector<PlannedEntry> entries;
  entries.reserve(static_cast<size_t>(drawlist.getNumCommands()));
  for (uint32_t commandIndex = 0;
       commandIndex < static_cast<uint32_t>(drawlist.getNumCommands());
       ++commandIndex) {
    const SoRenderCommand & command = drawlist.getCommand(
      static_cast<int>(commandIndex));
    SbMat view;
    const SbMatrix & effectiveView = command.state.useCommandMatrices
      ? command.viewMatrix : frameViewMatrix;
    effectiveView.getValue(view);
    SbVec3f worldCenter;
    command.modelMatrix.multVecMatrix(
      command.geometry.hasBounds ? command.geometry.boundsCenter
                                 : SbVec3f(0.0f, 0.0f, 0.0f),
      worldCenter);
    const float worldX = worldCenter[0];
    const float worldY = worldCenter[1];
    const float worldZ = worldCenter[2];
    const float eyeZ = view[0][2] * worldX +
      view[1][2] * worldY +
      view[2][2] * worldZ +
      view[3][2];

    PlannedEntry entry;
    entry.draw.commandIndex = commandIndex;
    entry.opacity = command.opacityClass;
    entry.depth = -eyeZ;
    entries.push_back(entry);
  }

  std::stable_sort(entries.begin(), entries.end(),
    [](const PlannedEntry & lhs, const PlannedEntry & rhs) {
      if (lhs.opacity != rhs.opacity) {
        return lhs.opacity == SO_OPACITY_OPAQUE;
      }
      if (lhs.opacity == SO_OPACITY_OPAQUE) {
        return false;
      }
      return lhs.depth > rhs.depth;
    });

  plan.draws.clear();
  plan.draws.reserve(entries.size());
  for (const PlannedEntry & entry : entries) {
    plan.draws.push_back(entry.draw);
  }
}
