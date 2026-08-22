#ifndef COIN_SORENDERMANAGER_H
#define COIN_SORENDERMANAGER_H

/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\**************************************************************************/

#include <Inventor/SbColor4f.h>
#include <Inventor/SbBox2s.h>
#include <Inventor/SbVec2s.h>

#include <cstdint>
#if COIN_HAVE_LEGACY_GL_RENDERER
#include <Inventor/actions/SoGLRenderAction.h>
#endif

class SbViewportRegion;
class SoEvent;
class SoPath;
class SoDetail;
class SoAction;
#if COIN_HAVE_LEGACY_GL_RENDERER
class SoGLRenderAction;
#endif
class SoAudioRenderAction;
class SoNode;
class SoCamera;
class SoNodeSensor;
class SoOneShotSensor;
class SoSensor;
class SoRenderManagerP;
class SoPickedPoint;
class SoPickedPointList;

typedef void SoRenderManagerRenderCB(void * userdata, class SoRenderManager * mgr);
typedef void SoRenderManagerStageCB(void * userdata,
                                    class SoRenderManager * mgr,
                                    SoAction * action);

/*!
  \class SoRenderManager SoRenderManager.h Inventor/SoRenderManager.h
  \brief Owns frame, stage, pipeline, and retained-backend lifecycle policy.

  The manager owns camera, viewport, device-pixel-ratio, callback, and scene
  traversal policy. It orchestrates Background, Main, AfterMain, and
  Foreground work; the retained pipeline records commands through
  SoIRRenderAction and delegates execution to a SoRenderBackend. The backend
  does not replace manager orchestration.

  \ingroup coin_retained_rendering
*/
class COIN_DLL_API SoRenderManager {
public:

  /*! Selects the scene execution pipeline for ordinary manager rendering. */
  enum class RenderPipeline {
    LEGACY_GL,
    DRAW_LIST
  };

  /*! Describes the pipeline outcome of the most recent render call. */
  struct RenderResult {
    enum class FallbackReason {
      NONE,
      MANAGER_FEATURE_UNSUPPORTED,
      CONTEXT_UNSUPPORTED,
      BACKEND_INITIALIZATION_FAILED,
      TRAVERSAL_UNSUPPORTED
    };

    RenderPipeline requestedPipeline;
    RenderPipeline usedPipeline;
    FallbackReason fallbackReason;
    SbBool rendered;
  };

  /*! Optional CPU timings for the most recent retained render and pick.

    Timing is disabled by default. When enabled, the manager records coarse
    orchestration phases without changing backend behavior. A zero duration
    means that the phase did not run, for example when a pick reused its
    existing pick buffer.
  */
  struct RenderPhaseStatistics {
    uint64_t drawListConstructionNanoseconds = 0;
    uint64_t drawListPrimitiveGenerationNanoseconds = 0;
    uint64_t drawListGeometryPackingNanoseconds = 0;
    uint64_t drawListCommandEmissionNanoseconds = 0;
    uint64_t planConstructionNanoseconds = 0;
    uint64_t backendSubmissionNanoseconds = 0;
    uint64_t backendFrameSetupNanoseconds = 0;
    uint64_t backendResourcePreparationNanoseconds = 0;
    uint64_t backendCommandExecutionNanoseconds = 0;
    uint64_t backendSelectionNanoseconds = 0;
    uint64_t drawListRebuilds = 0;
    //! Commands patched without rebuilding the retained frame.
    uint64_t incrementalCommandUpdates = 0;
    uint64_t pickPlanConstructionNanoseconds = 0;
    uint64_t pickBufferUpdateNanoseconds = 0;
    uint64_t pickQueryNanoseconds = 0;
    uint64_t pickResultResolutionNanoseconds = 0;
    uint64_t backendPickTargetPreparationNanoseconds = 0;
    uint64_t backendPickTargetRenderingNanoseconds = 0;
    uint64_t backendPickDepthRenderingNanoseconds = 0;
    uint64_t backendPickDepthPeelingNanoseconds = 0;
    uint64_t backendPickReadbackNanoseconds = 0;
    uint64_t backendPickHitProcessingNanoseconds = 0;
    uint64_t backendPickTargetRestoreNanoseconds = 0;
    uint64_t pickBufferRefreshes = 0;
  };

  class COIN_DLL_API Superimposition {
  public:
    enum StateFlags {
      ZBUFFERON    = 0x0001,
      CLEARZBUFFER = 0x0002,
      AUTOREDRAW   = 0x0004,
      BACKGROUND   = 0x0008
    };

    Superimposition(SoNode * scene,
                    SbBool enabled,
                    SoRenderManager * manager,
                    uint32_t flags);
    ~Superimposition();

#if COIN_HAVE_LEGACY_GL_RENDERER
    void render(SoGLRenderAction * action, SbBool clearcolorbuffer = FALSE);
#endif
    void setEnabled(SbBool yes);
    int getStateFlags(void) const;
#if COIN_HAVE_LEGACY_GL_RENDERER
    void setTransparencyType(SoGLRenderAction::TransparencyType transparencytype);
#endif

  private:
    static void changeCB(void * data, SoSensor * sensor);
    class SuperimpositionP * pimpl;
  };

  enum RenderMode {
    AS_IS,
    WIREFRAME,
    POINTS,
    WIREFRAME_OVERLAY,
    HIDDEN_LINE,
    BOUNDING_BOX,
    SHADED_HIDDEN_LINES
  };

  enum LightingMode {
    LIT,
    UNLIT
  };

  enum RenderLayer {
    RENDER_LAYER_BACKGROUND,
    RENDER_LAYER_FOREGROUND
  };

  enum StereoMode {
    MONO,
    ANAGLYPH,
    SEPARATE_OUTPUT,
    QUAD_BUFFER = SEPARATE_OUTPUT,
    INTERLEAVED_ROWS,
    INTERLEAVED_COLUMNS
  };

  enum BufferType {
    BUFFER_SINGLE,
    BUFFER_DOUBLE
  };

  enum AutoClippingStrategy {
    NO_AUTO_CLIPPING,
    FIXED_NEAR_PLANE,
    VARIABLE_NEAR_PLANE
  };

  SoRenderManager(void);
  virtual ~SoRenderManager();

  virtual void render(const SbBool clearwindow = TRUE,
                      const SbBool clearzbuffer = TRUE);

#if COIN_HAVE_LEGACY_GL_RENDERER
  virtual void render(SoGLRenderAction * action,
                      const SbBool initmatrices = TRUE,
                      const SbBool clearwindow = TRUE,
                      const SbBool clearzbuffer = TRUE);
#endif

  Superimposition * addSuperimposition(SoNode * scene,
                                       uint32_t flags =
                                       Superimposition::AUTOREDRAW |
                                       Superimposition::ZBUFFERON  |
                                       Superimposition::CLEARZBUFFER);
  void removeSuperimposition(Superimposition * s);

  virtual void setSceneGraph(SoNode * const sceneroot);
  virtual SoNode * getSceneGraph(void) const;

  void setCamera(SoCamera * camera);
  SoCamera * getCamera(void) const;

  /*!\brief Declare that the main scene graph contains the configured camera.

    The retained renderer uses this to preserve scene-graph camera ordering:
    state before the camera node starts from identity, just as in legacy
    traversal. The default is FALSE, meaning the configured camera is
    external to the scene graph.
  */
  void setCameraInSceneGraph(SbBool inSceneGraph);
  SbBool isCameraInSceneGraph(void) const;

  void setAutoClipping(AutoClippingStrategy autoclipping);
  AutoClippingStrategy getAutoClipping(void) const;
  void setNearPlaneValue(float value);
  float getNearPlaneValue(void) const;
  void setTexturesEnabled(const SbBool onoff);
  SbBool isTexturesEnabled(void) const;
  void setDoubleBuffer(const SbBool enable);
  SbBool isDoubleBuffer(void) const;
  void setRenderMode(const RenderMode mode);
  RenderMode getRenderMode(void) const;
  void setLightingMode(const LightingMode mode);
  LightingMode getLightingMode(void) const;
  void setStereoMode(const StereoMode mode);
  StereoMode getStereoMode(void) const;
  void setStereoOffset(const float offset);
  float getStereoOffset(void) const;

  void setRenderCallback(SoRenderManagerRenderCB * f,
                         void * const userData = NULL);

  SbBool isAutoRedraw(void) const;
  void setRedrawPriority(const uint32_t priority);
  uint32_t getRedrawPriority(void) const;

  void scheduleRedraw(void);
  void setWindowSize(const SbVec2s & newsize);
  void setDevicePixelRatio(float dpr);
  float getDevicePixelRatio(void) const;
  const SbVec2s & getWindowSize(void) const;
  void setSize(const SbVec2s & newsize);
  const SbVec2s & getSize(void) const;
  void setOrigin(const SbVec2s & newOrigin);
  const SbVec2s & getOrigin(void) const;
  void setViewportRegion(const SbViewportRegion & newRegion);
  const SbViewportRegion & getViewportRegion(void) const;
  void setBackgroundColor(const SbColor4f & color);
  const SbColor4f & getBackgroundColor(void) const;
  void setOverlayColor(const SbColor4f & color);
  SbColor4f getOverlayColor(void) const;
  void setBackgroundIndex(const int index);
  int getBackgroundIndex(void) const;
  void setRGBMode(const SbBool onOrOff);
  SbBool isRGBMode(void) const;
  virtual void activate(void);
  virtual void deactivate(void);

  void setAntialiasing(const SbBool smoothing, const int numPasses);
  void getAntialiasing(SbBool & smoothing, int & numPasses) const;
#if COIN_HAVE_LEGACY_GL_RENDERER
  void setGLRenderAction(SoGLRenderAction * const action);
  SoGLRenderAction * getGLRenderAction(void) const;
#endif
  void setRenderPipeline(RenderPipeline pipeline);
  RenderPipeline getRenderPipeline(void) const;
  SbBool isRenderPipelineAvailable(RenderPipeline pipeline) const;
  const RenderResult & getLastRenderResult(void) const;

  //! Enable coarse retained-renderer CPU timing for diagnostics.
  void setRenderPhaseTimingEnabled(SbBool enabled);
  SbBool isRenderPhaseTimingEnabled(void) const;
  RenderPhaseStatistics getRenderPhaseStatistics(void) const;

  /*! Return the closest renderer-neutral scene hit. The caller owns result. */
  SbBool pickClosest(int x, int y, int radius, SoPickedPoint *& result);
  /*! Return front-to-back renderer-neutral scene hits around a cursor. */
  SbBool pickDepthStack(int x, int y, int radius, int maxLayers,
                        SoPickedPointList & results, int maxHits = 32);
  /*! Return deduplicated visible scene hits in a viewport-local region. */
  SbBool pickVisibleRegion(const SbBox2s & region,
                           SoPickedPointList & results);

  //! Notify the active renderer that external GL state may have changed.
  void invalidateSharedGLState(void);

  /*! Release backend API resources while its owning GL context is current.

      Calling this with another or no context is invalid; use
      discardRenderBackendResources() after context loss instead. */
  void releaseRenderBackendResources(void);

  /*! Forget backend API resource handles after the owning context is lost.

      This operation deliberately issues no GL deletion calls. */
  void discardRenderBackendResources(void);

  void setRenderLayerRoot(RenderLayer layer, SoNode * root);
  SoNode * getRenderLayerRoot(RenderLayer layer) const;

  //! Invalidate the retained main-scene frame and schedule a redraw.
  void invalidateDrawList(void);
  void invalidateScene(void);
  void invalidateForeground(void);

  void setAudioRenderAction(SoAudioRenderAction * const action);
  SoAudioRenderAction * getAudioRenderAction(void) const;

  static void enableRealTimeUpdate(const SbBool flag);
  static SbBool isRealTimeUpdateEnabled(void);
  static uint32_t getDefaultRedrawPriority(void);

  void addPreRenderCallback(SoRenderManagerRenderCB * cb, void * data);
  void removePreRenderCallback(SoRenderManagerRenderCB * cb, void * data);

  void addPostRenderCallback(SoRenderManagerRenderCB * cb, void * data);
  void removePostRenderCallback(SoRenderManagerRenderCB * cb, void * data);

  void addAfterMainSceneCallback(SoRenderManagerStageCB * cb, void * data);
  void removeAfterMainSceneCallback(SoRenderManagerStageCB * cb, void * data);

  void reinitialize(void);

protected:
  int isActive(void) const;
  void redraw(void);

#if COIN_HAVE_LEGACY_GL_RENDERER
  void renderScene(SoGLRenderAction * action,
                   SoNode * scene,
                   uint32_t clearmask);

  void actuallyRender(SoGLRenderAction * action,
                      const SbBool initmatrices = TRUE,
                      const SbBool clearwindow = TRUE,
                      const SbBool clearzbuffer = TRUE);

  void renderSingle(SoGLRenderAction * action,
                    SbBool initmatrices,
                    SbBool clearwindow,
                    SbBool clearzbuffer);

  void renderStereo(SoGLRenderAction * action,
                    SbBool initmatrices,
                    SbBool clearwindow,
                    SbBool clearzbuffer);

  void initStencilBufferForInterleavedStereo(void);
  void clearBuffers(SbBool color, SbBool depth);
#endif

private:
  void renderDrawListPipeline(SbBool clearwindow, SbBool clearzbuffer);

  void attachRootSensor(SoNode * const sceneroot);
  void attachClipSensor(SoNode * const sceneroot);
  void detachRootSensor(void);
  void detachClipSensor(void);
  static void nodesensorCB(void * data, SoSensor *);
#if COIN_HAVE_LEGACY_GL_RENDERER
  static void prerendercb(void * userdata, SoGLRenderAction * action);
#endif

  SoRenderManagerP * pimpl;
  friend class SoRenderManagerP;
  friend class SoSceneManager;
  friend class Superimposition;

}; // SoRenderManager

#endif // !COIN_SORENDERMANAGER_H
