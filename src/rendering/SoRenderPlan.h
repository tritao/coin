
#ifndef COIN_SORENDERPLAN_H
#define COIN_SORENDERPLAN_H

#include <Inventor/rendering/SoRenderIR.h>

#include <cstdint>
#include <vector>

/*!
  \class SoRenderCommandTraits
  \brief Shared semantic comparisons used by planning and submission.

  These comparisons describe which command state may be shared without
  changing rendering semantics. Device capability checks remain backend-owned.
*/
class SoRenderCommandTraits {
public:
  enum class PlanningClass : uint8_t {
    OPAQUE_INSERTION_ORDER,
    TRANSPARENT_DEPTH_SORTED
  };

  enum class OpaqueGroup : uint8_t {
    TRIANGLES,
    NATIVE_LINES,
    NONE
  };

  static bool sameMaterialUniformState(const SoMaterialData & lhs,
                                       const SoMaterialData & rhs);
  static bool sameInstancedMaterialState(const SoMaterialData & lhs,
                                         const SoMaterialData & rhs);
  static bool sameTextureBinding(const SoTextureData & lhs,
                                 const SoTextureData & rhs);
  static bool sameBlendState(const SoBlendState & lhs,
                             const SoBlendState & rhs);
  static OpaqueGroup classifyOpaqueGroup(const SoRenderCommand & command,
                                         const SoGeometryDesc & geometry);
  static PlanningClass classifyPlanning(const SoRenderCommand & command);
  static bool transformAffectsPlanning(const SoRenderCommand & command);
};

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
  Opaque commands retain insertion order. Adjacent compatible triangle or
  native-line commands may still be submitted as one instanced batch by the
  backend. Transparent commands are resolved back-to-front within each depth
  segment. The plan also owns the stage and depth-barrier execution sequence.
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
class SoRenderPlanner {
public:
  //! Resolve semantic command state into backend execution order.
  void build(const SoDrawList & drawlist, SoRenderPlan & plan) const;
};

#endif // COIN_SORENDERPLAN_H
