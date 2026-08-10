// include/Inventor/actions/SoIRRenderAction.h

#ifndef COIN_SOIRRENDERACTION_H
#define COIN_SOIRRENDERACTION_H

#include <Inventor/actions/SoAction.h>
#include <Inventor/actions/SoSubAction.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/lists/SbList.h>

#include <Inventor/rendering/SoRenderIR.h>

#include <cstddef>
#include <memory>
class SoPrimitiveVertex;
class SoPath;
class SoPathList;
class SoCamera;
class SoIRRenderActionP;

/*!
  \class SoIRRenderAction SoIRRenderAction.h
  \brief Render action that traverses a scene graph into a backend-neutral draw list.

  \ingroup coin_actions

  SoIRRenderAction is the traversal front-end for Coin's render-backend path.
  Unlike SoGLRenderAction, it does not issue OpenGL commands directly during
  traversal. Instead it records geometry, material state, and render state
  into a SoDrawList that can later be consumed by a concrete backend.

  The action owns transient per-frame storage for generated geometry.
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

  /*! \brief Opaque checkpoint for partial frame geometry rebuilds. */
  class GeometrySavePoint {
  public:
    GeometrySavePoint() = default;

  private:
    struct Data;
    explicit GeometrySavePoint(const std::shared_ptr<Data> & data)
      : data(data) { }
    std::shared_ptr<Data> data;
    friend class SoIRBuffer;
  };

  static void initClass(void);

  SoIRRenderAction(const SbViewportRegion & vp);
  virtual ~SoIRRenderAction();

  //! Clear the current draw list and begin a new retained frame.
  void beginFrame();

  void setViewportRegion(const SbViewportRegion & vp);
  const SbViewportRegion & getViewportRegion(void) const { return this->vpRegion; }

  void setCacheContext(uint32_t context) { this->cacheContext = context; }
  uint32_t getCacheContext(void) const { return this->cacheContext; }
  void setCamera(SoCamera * camera) { this->camera = camera; }
  SoCamera * getCamera(void) const { return this->camera; }

  // Standard entry points, mirroring SoGLRenderAction
  virtual void apply(SoNode * root) override;
  virtual void apply(SoPath * path) override;
  virtual void apply(const SoPathList & pathlist, SbBool obeysrules = FALSE) override;

  //! Append a root without clearing the current retained frame.
  void traverseAdditionalRoot(SoNode * root);

  void beginAfterMainStage();
  void endAfterMainStage();
  bool isAfterMainStage() const { return this->afterMainStageDepth != 0; }
  SoRenderStage getRenderStage() const { return this->renderStage; }
  void setRenderStage(SoRenderStage stage) { this->renderStage = stage; }
  void applyRenderStage(SoRenderCommand & command);

  //! Associate the commands emitted by a shape with its current scene path.
  void storeCommandPath(int commandIndex, const SoPath * path);
  //! Return the retained scene path for a command, or NULL when unavailable.
  SoPath * getCommandPath(int commandIndex) const;

  //! Return the generated draw list for the current frame.
  const SoDrawList & getDrawList(void) const { return this->drawlist; }
  //! Mutable access to the generated draw list for the current frame.
  SoDrawList & getMutableDrawList() { return this->drawlist; }

  /*!
    \brief Allocate per-frame geometry storage owned by the action.

    The returned memory remains valid until the frame resources are cleared or
    the geometry pool is rewound to an earlier save point.
  */
  void * allocateGeometryStorage(size_t bytes, size_t alignment = alignof(float));

  GeometrySavePoint saveGeometryPool() const;
  void rewindGeometryPool(const GeometrySavePoint & savepoint);

  //! Clear all transient geometry owned by the current frame.
  void clearGeometryPool();

  //! Push a primitive collector for subsequent fallback primitive generation.
  void pushPrimitiveCollector(PrimitiveCollector * collector);
  //! Pop the current primitive collector. The caller must pop in stack order.
  void popPrimitiveCollector(PrimitiveCollector * collector);
  //! Return the currently active primitive collector, or NULL.
  PrimitiveCollector * getActivePrimitiveCollector(void) const;

  void setHasCameraDependentShapes(bool value) { this->cameraDependentShapes = value; }
  bool hasCameraDependentShapes() const { return this->cameraDependentShapes; }

  // (later) hooks for backend:
  // uint32_t getCacheContext(void) const;
  // void setCacheContext(uint32_t ctx);

protected:
  virtual void beginTraversal(SoNode * node) override;
  virtual void endTraversal(SoNode * node) override;

  // static dispatchers installed in SoAction's method table
  static void renderNode(SoAction * action, SoNode * node);
  static void renderShape(SoAction * action, SoNode * node);
  static void renderShaderProgram(SoAction * action, SoNode * node);

private:
  void resetFrameResources();

  SbViewportRegion vpRegion;
  SoCamera *       camera = nullptr;
  SoDrawList       drawlist;
  SoIRRenderActionP * pimpl;
  uint32_t          cacheContext = 0;
  bool              cameraDependentShapes = false;
  unsigned int      afterMainStageDepth = 0;
  bool              afterMainDepthClearPending = false;
  SoRenderStage     renderStage = SoRenderStage::Main;
};

/*! \brief RAII guard for an action-local manager render stage. */
class COIN_DLL_API SoIRRenderStageScope {
public:
  SoIRRenderStageScope(SoIRRenderAction & action, SoRenderStage stage);
  ~SoIRRenderStageScope();

  SoIRRenderStageScope(const SoIRRenderStageScope &) = delete;
  SoIRRenderStageScope & operator=(const SoIRRenderStageScope &) = delete;

private:
  SoIRRenderAction * action;
  SoRenderStage previousStage;
};

#endif // COIN_SOIRRENDERACTION_H
