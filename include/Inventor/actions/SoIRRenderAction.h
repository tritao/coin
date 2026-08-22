// include/Inventor/actions/SoIRRenderAction.h

#ifndef COIN_SOIRRENDERACTION_H
#define COIN_SOIRRENDERACTION_H

#include <Inventor/actions/SoAction.h>
#include <Inventor/actions/SoSubAction.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/lists/SbList.h>

#include <Inventor/rendering/SoRenderIR.h>

#include <cstddef>
#include <vector>
class SoPrimitiveVertex;
class SoPath;
class SoPathList;
class SoCamera;
class SoNode;
class SoIRRenderActionP;
class SoRetainedMutationTransaction;

/*!
  \class SoIRRenderAction SoIRRenderAction.h
  \brief Render action that traverses a scene graph into a backend-neutral draw list.

  \ingroup coin_actions

  SoIRRenderAction is the traversal front-end for Coin's render-backend path.
  Unlike SoGLRenderAction, it does not issue OpenGL commands directly during
  traversal. Instead it records geometry, material state, and render state
  into a SoDrawList that can later be consumed by a concrete backend.

  The action owns transient per-frame storage for generated geometry.

  A frame begins with beginFrame() (also performed by the normal apply
  entry points), records commands through addCommand(), and ends when the
  caller replaces or clears the action's frame. Geometry and other borrowed
  command data must not be retained beyond that frame lifetime.

  \ingroup coin_retained_rendering
*/
class COIN_DLL_API SoIRRenderAction : public SoAction {
  typedef SoAction inherited;
  SO_ACTION_HEADER(SoIRRenderAction);

public:
  struct ConstructionStatistics {
    uint64_t primitiveGenerationNanoseconds = 0;
    uint64_t geometryPackingNanoseconds = 0;
    uint64_t commandEmissionNanoseconds = 0;
  };

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
    //! Primitive attributes needed by the retained renderer.
    struct VertexData {
      SbVec3f point;
      SbVec3f normal;
      SbVec4f texcoord = SbVec4f(0.0f, 0.0f, 0.0f, 1.0f);
      int materialIndex = 0;
    };
    virtual ~PrimitiveCollector() {}
    virtual void onTriangle(const SoPrimitiveVertex * v1,
                            const SoPrimitiveVertex * v2,
                            const SoPrimitiveVertex * v3) = 0;
    virtual void onLine(const SoPrimitiveVertex * v1,
                        const SoPrimitiveVertex * v2) = 0;
    virtual void onPoint(const SoPrimitiveVertex * v) = 0;
    //! Receive an already resolved triangle and its picking identity.
    virtual void onTriangleData(const VertexData & v1,
                                const VertexData & v2,
                                const VertexData & v3,
                                int faceIndex) = 0;
    //! Receive an already resolved line segment and its picking identity.
    virtual void onLineData(const VertexData & v1,
                            const VertexData & v2,
                            int lineIndex) = 0;
    //! Reuse or register an explicit non-textured triangle source.
    virtual SbBool beginRetainedTriangles(uint64_t sourceKey,
                                          uint64_t revision,
                                          int faceCount)
    { return FALSE; }
    //! Reuse a previously registered triangle source without rescanning it.
    virtual SbBool reuseRetainedTriangles(uint64_t sourceId,
                                          uint64_t revision)
    { return FALSE; }
    //! Reuse or register an explicit non-textured line-segment source.
    virtual SbBool beginRetainedLines(uint64_t sourceId,
                                      uint64_t revision,
                                      int segmentCount)
    { return FALSE; }
    virtual SbBool reuseRetainedLines(uint64_t sourceId,
                                      uint64_t revision)
    { return FALSE; }
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

  //! Append a retained command produced during the current traversal.
  void addCommand(const SoRenderCommand & command);

  //! Mark the current frame as unsupported by the retained renderer.
  void markUnsupported(const SoNode * node, const char * reason);
  //! Return whether traversal encountered semantics not represented by IR.
  SbBool hasUnsupportedRendering() const { return this->unsupportedRendering; }
  //! Return the first node that made this frame unsupported, if any.
  const SoNode * getUnsupportedNode() const { return this->unsupportedNode; }
  //! Return a static or otherwise frame-stable explanation for the status.
  const char * getUnsupportedReason() const { return this->unsupportedReason; }
  //! Return the non-owned path retained for a command in this frame.
  //! The pointer is borrowed and remains valid until the next apply/beginFrame
  //! on this action or until the action is destroyed.
  const SoPath * getCommandPath(int commandIndex) const;

  //! Append a root without clearing the current retained frame.
  void traverseAdditionalRoot(
    SoNode * root,
    CameraPolicy policy = CameraPolicy::USE_CONFIGURED_CAMERA);

  //! Traverse a path without clearing the retained frame. Ancestor traversal
  //! reconstructs inherited scene state for the replayed path.
  void traverseAdditionalPath(SoPath * path);
#ifdef COIN_INTERNAL
  //! Traverse a path using a copied replay context. The context supplements
  //! path traversal; it is not a general snapshot of SoState.
  void traverseAdditionalPath(SoPath * path,
                              const SoIRRenderContext & context);
#endif

  //! Record a depth-clear barrier at the current traversal position.
  void requestDepthClear();
#ifdef COIN_INTERNAL
  SoRenderStage getRenderStage() const;
  void setRenderStage(SoRenderStage stage);
  void applyRenderStage(SoRenderCommand & command);
  //! Refresh matrices for commands affected by one state-node notification.
  int updateCommandMatricesForStatePath(const SoPath * statePath);
  //! Refresh unique commands affected by a batch of transform notifications.
  int updateCommandMatricesForStatePaths(
    const std::vector<const SoPath *> & statePaths);
  //! Return whether moving affected commands changes planner ordering.
  SbBool transformUpdateAffectsPlanning(
    const std::vector<const SoPath *> & statePaths) const;
  //! Refresh effective diffuse colors after one material notification.
  int updateCommandDiffuseColorsForStatePath(const SoPath * statePath);
  //! Refresh unique commands affected by diffuse-color notifications.
  int updateCommandDiffuseColorsForStatePaths(
    const std::vector<const SoPath *> & statePaths);
  //! Toggle commands below a stable one-child switch.
  int updateCommandVisibilityForSwitchPath(const SoPath * switchPath,
                                           SbBool visible);
  //! Regenerate a geometry resource affected by one state notification.
  int updateCommandGeometryForStatePath(const SoPath * statePath);
  //! Regenerate one completely-owned resource affected by changed paths.
  int updateCommandGeometryForStatePaths(
    const std::vector<const SoPath *> & statePaths);
#endif


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

  //! Copy one texture payload into the current frame, reusing an existing copy.
  const unsigned char * allocateTextureStorage(const unsigned char * source,
                                               size_t bytes,
                                               int width,
                                               int height,
                                               int numComponents,
                                               bool & hasTransparency);

  //! Push a primitive collector for subsequent fallback primitive generation.
  void pushPrimitiveCollector(PrimitiveCollector * collector);
  //! Pop the current primitive collector. The caller must pop in stack order.
  void popPrimitiveCollector(PrimitiveCollector * collector);
  //! Return the currently active primitive collector, or NULL.
  PrimitiveCollector * getActivePrimitiveCollector(void) const;
  //! Find a geometry resource with the same producer identity this frame.
  SoGeometryHandle findGeometrySource(uint64_t sourceKey,
                                      uint64_t revision) const;
  //! Enable intrusive construction attribution for benchmark diagnostics.
  void setConstructionTimingEnabled(SbBool enabled);
  SbBool isConstructionTimingEnabled() const;
  const ConstructionStatistics & getConstructionStatistics() const;
  void recordPrimitiveGenerationNanoseconds(uint64_t nanoseconds);
  void recordGeometryPackingNanoseconds(uint64_t nanoseconds);
  void recordCommandEmissionNanoseconds(uint64_t nanoseconds);

protected:
  virtual void beginTraversal(SoNode * node) override;

private:
  friend class SoRetainedMutationTransaction;
  void initializeCameraState(CameraPolicy policy);
  void resetFrameResources();
  void clearCommandPaths();
#ifdef COIN_INTERNAL
  void findCommandsAffectedByStatePath(
    const SoPath * statePath, std::vector<size_t> & commandIndices) const;
  void findCommandsAffectedByStatePaths(
    const std::vector<const SoPath *> & statePaths,
    std::vector<size_t> & commandIndices) const;
  void traverseAdditionalPathInternal(
    SoPath * path, const SoIRRenderContext * context);
  const SoIRRenderContext * getRenderContextOverride() const;
#endif

  SbViewportRegion vpRegion;
  SoCamera *       camera = nullptr;
  CameraPolicy     cameraPolicy = CameraPolicy::USE_CONFIGURED_CAMERA;
  float            devicePixelRatio = 1.0f;
  SoDrawList       drawlist;
  std::vector<SoPath *> commandPaths;
  SoIRRenderActionP * pimpl;
  bool unsupportedRendering = false;
  const SoNode * unsupportedNode = nullptr;
  const char * unsupportedReason = nullptr;
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
