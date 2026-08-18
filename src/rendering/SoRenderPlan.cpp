
#include "rendering/SoRenderPlan.h"

void
SoRenderPlanner::build(const SoDrawList & drawlist,
                       const SbMatrix &,
                       SoRenderPlan & plan) const
{
  plan.draws.clear();
  plan.draws.reserve(static_cast<size_t>(drawlist.getNumCommands()));
  for (uint32_t commandIndex = 0;
       commandIndex < static_cast<uint32_t>(drawlist.getNumCommands());
       ++commandIndex) {
    SoPlannedDraw draw;
    draw.commandIndex = commandIndex;
    plan.draws.push_back(draw);
  }
}
