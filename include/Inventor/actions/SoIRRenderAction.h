// include/Inventor/actions/SoIRRenderAction.h

#ifndef COIN_SOIRRENDERACTION_H
#define COIN_SOIRRENDERACTION_H

#include <Inventor/actions/SoAction.h>
#include <Inventor/actions/SoSubAction.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/lists/SbList.h>

#include <Inventor/rendering/SoRenderIR.h>

#include <cstddef>
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
  /*! Camera state policy used when starting a root traversal. */
  enum class CameraPolicy {
    //! Initialize the traversal from the camera configured on this action.
    USE_CONFIGURED_CAMERA,
    //! Start with the current state and let a camera node in the root set it.
    CAMERA_IN_ROOT
  };

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

  //! Clear the current draw list and begin a new retained frame.
  void beginFrame();

  void setViewportRegion(const SbViewportRegion & vp);
  const SbViewportRegion & getViewportRegion(void) const { return this->vpRegion; }

  void setCamera(SoCamera * camera) { this->camera = camera; }
  SoCamera * getCamera(void) const { return this->camera; }
  void setCameraPolicy(CameraPolicy policy) { this->cameraPolicy = policy; }
  CameraPolicy getCameraPolicy(void) const { return this->cameraPolicy; }
  void setDevicePixelRatio(float dpr) { this->devicePixelRatio = dpr; }
  float getDevicePixelRatio(void) const { return this->devicePixelRatio; }

  // Standard entry points, mirroring SoGLRenderAction
  virtual void apply(SoNode * root) override;
  virtual void apply(SoPath * path) override;
  virtual void apply(const SoPathList & pathlist, SbBool obeysrules = FALSE) override;

  //! Append a root without clearing the current retained frame.
  void traverseAdditionalRoot(
    SoNode * root,
    CameraPolicy policy = CameraPolicy::USE_CONFIGURED_CAMERA);

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

  //! Copy texture bytes into action-owned frame storage, reusing an existing
  //! copy when the same source image is referenced more than once.
  const unsigned char * retainTextureData(const unsigned char * bytes,
                                          size_t byteCount);

  //! Clear all transient geometry owned by the current frame.
  void clearGeometryPool();

  //! Push a primitive collector for subsequent fallback primitive generation.
  void pushPrimitiveCollector(PrimitiveCollector * collector);
  //! Pop the current primitive collector. The caller must pop in stack order.
  void popPrimitiveCollector(PrimitiveCollector * collector);
  //! Return the currently active primitive collector, or NULL.
  PrimitiveCollector * getActivePrimitiveCollector(void) const;

protected:
  virtual void beginTraversal(SoNode * node) override;
  virtual void endTraversal(SoNode * node) override;

  // static dispatchers installed in SoAction's method table
  static void renderNode(SoAction * action, SoNode * node);
  static void renderShape(SoAction * action, SoNode * node);

private:
  void initializeCameraState(CameraPolicy policy);
  void resetFrameResources();

  SbViewportRegion vpRegion;
  SoCamera *       camera = nullptr;
  CameraPolicy     cameraPolicy = CameraPolicy::USE_CONFIGURED_CAMERA;
  float            devicePixelRatio = 1.0f;
  SoDrawList       drawlist;
  SoIRRenderActionP * pimpl;
  unsigned int     afterMainStageDepth = 0;
  bool             afterMainDepthClearPending = false;
  SoRenderStage    renderStage = SoRenderStage::Main;
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
