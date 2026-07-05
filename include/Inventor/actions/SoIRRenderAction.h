// include/Inventor/actions/SoIRRenderAction.h

#ifndef COIN_SOIRRENDERACTION_H
#define COIN_SOIRRENDERACTION_H

#include <Inventor/actions/SoAction.h>
#include <Inventor/actions/SoSubAction.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/lists/SbList.h>

#include <Inventor/rendering/SoRenderIR.h>

#include <cstddef>

class SoPath;
class SoPathList;
class SoCamera;
class SoPrimitiveVertex;
class SoShape;
class SoIRRenderActionP;

/*!
  \class SoIRRenderAction SoIRRenderAction.h
  \brief Render action that traverses a scene graph into a backend-neutral draw list.

  \ingroup coin_actions

  SoIRRenderAction is the traversal front-end for Coin's render-backend path.
  Unlike SoGLRenderAction, it does not issue OpenGL commands directly during
  traversal. Instead it records geometry, material state, render state,
  layering information, and pick metadata into a SoDrawList that can later be
  consumed by a concrete backend.

  The action owns transient per-frame storage for generated geometry and can
  append additional scene roots after the main traversal. This is used for
  explicit background and foreground layer roots managed by SoRenderManager.
*/
class COIN_DLL_API SoIRRenderAction : public SoAction {
  typedef SoAction inherited;
  SO_ACTION_HEADER(SoIRRenderAction);

public:
  /*!
    \class SoIRRenderAction::PrimitiveCollector
    \brief Callback interface for receiving primitives generated during traversal.

    Shapes that fall back to generatePrimitives() can stream their output
    through a PrimitiveCollector instead of building temporary Coin-specific
    callback structures. The active collector is managed as a stack so helper
    code can install a collector for a limited traversal scope.
  */
  class PrimitiveCollector {
  public:
    virtual ~PrimitiveCollector() {}
    virtual void onTriangle(const SoPrimitiveVertex * v1,
                            const SoPrimitiveVertex * v2,
                            const SoPrimitiveVertex * v3) = 0;
    virtual void onLine(const SoPrimitiveVertex * v1,
                        const SoPrimitiveVertex * v2) = 0;
    virtual void onPoint(const SoPrimitiveVertex * v) = 0;
  };

  static void initClass(void);

  SoIRRenderAction(const SbViewportRegion & vp);
  virtual ~SoIRRenderAction();

  void setViewportRegion(const SbViewportRegion & vp);
  const SbViewportRegion & getViewportRegion(void) const { return this->vpRegion; }

  void setCamera(SoCamera * camera);
  SoCamera * getCamera(void) const { return this->camera; }

  // Standard entry points, mirroring SoGLRenderAction
  virtual void apply(SoNode * root);
  virtual void apply(SoPath * path);
  virtual void apply(const SoPathList & pathlist, SbBool obeysrules = FALSE);

  /*!
    \brief Traverse an extra scene root without clearing the current draw list.

    Commands collected from this traversal are appended to the existing frame.
    This is primarily used for explicit render-layer roots that should render
    before or after the main scene.
  */
  void traverseAdditionalRoot(SoNode * root);

  //! Return the generated draw list for the current frame.
  const SoDrawList & getDrawList(void) const { return this->drawlist; }
  //! Mutable access to the generated draw list for the current frame.
  SoDrawList & getMutableDrawList() { return this->drawlist; }

  /*!
    \brief Store the scene graph path associated with a draw command.

    Called by shapes during render() so later picking/highlighting code can
    resolve a draw-list command back to the originating scene graph path.
    Implementations copy and ref the path and release it when the frame data
    is cleared.
  */
  void storeCommandPath(int commandIndex, const SoPath * path);

  //! Return the stored scene graph path for a command index, or NULL.
  SoPath * getCommandPath(int commandIndex) const;

  /*!
    \brief Allocate per-frame geometry storage owned by the action.

    The returned memory remains valid until the frame resources are cleared or
    the geometry pool is rewound to an earlier save point.
  */
  void * allocateGeometryStorage(size_t bytes, size_t alignment = alignof(float));

  /*!
    \brief Save the current geometry-pool allocation position.

    Save points are used for partial draw-list rebuilds. A caller can save the
    allocation state after main-scene traversal and later rewind before
    re-traversing a foreground layer so geometry is reallocated at the same
    addresses and backend cache keys remain stable.
  */
  SoIRBuffer::SavePoint saveGeometryPool() const;
  //! Rewind the geometry pool to a previously captured save point.
  void rewindGeometryPool(const SoIRBuffer::SavePoint & sp);
  //! Clear all transient geometry owned by the current frame.
  void clearGeometryPool();

  //! Push a primitive collector for subsequent fallback primitive generation.
  void pushPrimitiveCollector(PrimitiveCollector * collector);
  //! Pop the current primitive collector. The caller must pop in stack order.
  void popPrimitiveCollector(PrimitiveCollector * collector);
  //! Return the currently active primitive collector, or NULL.
  PrimitiveCollector * getActivePrimitiveCollector(void) const;

  /*!
    \brief Mark whether the scene contains camera-dependent generated content.

    Examples include viewport-aligned labels whose geometry changes with the
    camera. SoRenderManager can use this hint to decide when the draw list must
    be rebuilt after a camera change.
  */
  void setHasCameraDependentShapes(bool v) { cameraDependentShapes = v; }
  //! Return whether this frame contains camera-dependent generated content.
  bool hasCameraDependentShapes() const { return cameraDependentShapes; }

  // (later) hooks for backend:
  // uint32_t getCacheContext(void) const;
  // void setCacheContext(uint32_t ctx);

protected:
  virtual void beginTraversal(SoNode * node);
  virtual void endTraversal(SoNode * node);

  // static dispatchers installed in SoAction's method table
  static void renderNode(SoAction * action, SoNode * node);
  static void renderShape(SoAction * action, SoNode * node);
  static void renderShaderProgram(SoAction * action, SoNode * node);

private:
  void resetFrameResources();

  SbViewportRegion vpRegion;
  SoCamera *       camera;

  SoDrawList       drawlist;
  SoIRRenderActionP * pimpl;
  bool             cameraDependentShapes = false;
};

#endif // COIN_SOIRRENDERACTION_H
