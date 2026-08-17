
#ifndef COIN_SORENDERPLAN_H
#define COIN_SORENDERPLAN_H

#include <Inventor/rendering/SoRenderIR.h>

#include <cstdint>
#include <vector>

/*!
  \struct SoPlannedDraw
  \brief One draw operation in a resolved retained-render plan.
*/
struct SoPlannedDraw {
  uint32_t commandIndex = 0;
};

/*!
  \class SoRenderPlan
  \brief Internal execution sequence for one retained frame.

  The plan owns no command data. It contains only stable indices into the
  source SoDrawList and is therefore cheap to rebuild for each invocation.
*/
class SoRenderPlan {
public:
  int getNumDraws() const
  { return static_cast<int>(this->draws.size()); }

  const SoPlannedDraw & getDraw(int index) const
  { return this->draws[static_cast<size_t>(index)]; }

private:
  std::vector<SoPlannedDraw> draws;
  friend class SoRenderPlanner;
};

/*!
  \class SoRenderPlanner
  \brief Internal owner of retained-frame execution ordering.
*/
class COIN_DLL_API SoRenderPlanner {
public:
  void build(const SoDrawList & drawlist, SoRenderPlan & plan) const;
};

#endif // COIN_SORENDERPLAN_H
