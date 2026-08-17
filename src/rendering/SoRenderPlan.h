
#ifndef COIN_SORENDERPLAN_H
#define COIN_SORENDERPLAN_H

#include <Inventor/rendering/SoRenderIR.h>

#include <cstdint>
#include <vector>

/*!
  \struct SoPlannedDraw
  \brief One draw operation in a resolved retained-render plan.
*/
enum class SoRenderOperationType : uint8_t {
  DRAW,
  END_DEPTH_SEGMENT,
  CLEAR_DEPTH
};

struct SoRenderOperation {
  SoRenderOperationType type = SoRenderOperationType::DRAW;
  uint32_t commandIndex = 0;
  uint32_t depthClearEventIndex = 0;
};

/*!
  \class SoRenderPlan
  \brief Internal execution sequence for one retained frame.

  The plan owns no command data. It contains only stable indices into the
  source SoDrawList and is therefore cheap to rebuild for each invocation.
  Opaque commands retain insertion order; transparent commands are resolved
  back-to-front after opaque commands within each depth segment.  The plan
  also owns the stage and depth-barrier execution sequence.
*/
class SoRenderPlan {
public:
  int getNumOperations() const
  { return static_cast<int>(this->operations.size()); }

  const SoRenderOperation & getOperation(int index) const
  { return this->operations[static_cast<size_t>(index)]; }

private:
  std::vector<SoRenderOperation> operations;
  friend class SoRenderPlanner;
};

/*!
  \class SoRenderPlanner
  \brief Internal owner of retained-frame execution ordering.
*/
class COIN_DLL_API SoRenderPlanner {
public:
  //! Resolve semantic command state into backend execution order.
  void build(const SoDrawList & drawlist, const SbMatrix & frameViewMatrix,
             SoRenderPlan & plan) const;
};

#endif // COIN_SORENDERPLAN_H
