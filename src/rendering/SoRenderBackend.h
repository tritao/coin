// src/rendering/SoRenderBackend.h

#ifndef COIN_SORENDERBACKEND_H
#define COIN_SORENDERBACKEND_H

#include <Inventor/SbBasic.h>
#include <Inventor/SbBox2s.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/rendering/SoRenderIR.h>

#include "rendering/SoRenderPlan.h"

#include <cstdint>

class SoDrawList;

typedef void (*SoRenderBackendLogFn)(const char * message, void * userdata);

/*!
  \struct SoRenderParams
  \brief Per-render values consumed by a retained-rendering backend.

  The values describe the currently bound framebuffer and the view being
  rendered into it.  Target ownership and application orchestration remain
  outside this interface.
*/
struct SoRenderParams {
  SbViewportRegion viewport;
  SbMatrix         viewMatrix;
  SbMatrix         projMatrix;
  float            devicePixelRatio = 1.0f;
  SbColor4f        clearColor;
  float            clearDepth = 1.0f;
  uint32_t         flags = 0;
};

/*!
  \struct SoRenderBackendInitParams
  \brief Minimal backend initialization hooks.
*/
struct SoRenderBackendInitParams {
  void *               userData = nullptr;
  SoRenderBackendLogFn logCallback = nullptr;
  SoRenderBackendLogFn errorCallback = nullptr;
};

/*!
  \class SoRenderBackend
  \brief Backend-neutral lifecycle and DrawList execution interface.

  The retained IR does not depend on this interface or on a graphics API.
  Concrete backends own all device resources.

  A backend consumes a complete retained frame but does not own scene-graph
  orchestration, target selection, or the storage referenced by commands.
  The caller must keep the draw list and its borrowed frame data alive for the
  duration of render().

  \ingroup coin_retained_rendering
*/
class SoRenderBackend {
public:
  SoRenderBackend();
  virtual ~SoRenderBackend();

  virtual const char * getName() const = 0;

  virtual SbBool initialize(const SoRenderBackendInitParams & params) = 0;
  virtual void shutdown() = 0;
  virtual SbBool render(const SoDrawList & drawlist,
                        const SoRenderPlan & plan,
                        const SoRenderParams & params) = 0;

  //! Update the backend's explicit picking target for a retained frame.
  //! Ordinary render() does not implicitly regenerate this target.
  virtual SbBool updatePickBuffer(const SoDrawList & drawlist,
                                  const SoRenderPlan & plan,
                                  const SoRenderParams & params);
  //! Resolve the closest viewport-local hit against the last explicit update.
  virtual SbBool pickClosest(int x, int y, int radius,
                             SoPickResult & result);
  //! Resolve and deduplicate the visible IDs in a viewport-local region.
  virtual SbBool pickVisibleRegion(const SbBox2s & region,
                                   SoPickResultList & results);
  //! Resolve a bounded front-to-back stack around a viewport-local cursor.
  virtual SbBool pickDepthStack(int x, int y, int radius, int maxLayers,
                                int maxHits,
                                SoPickResultList & results);
  //! Compatibility spelling for the original closest-hit backend query.
  SbBool pick(int x, int y, int radius, SoPickResult & result)
  { return this->pickClosest(x, y, radius, result); }

  //! Render frame-level selection/highlight targets over the current frame.
  virtual SbBool renderSelection(const SoDrawList & drawlist,
                                 const SoSelectionState & selection,
                                 const SoRenderParams & params);

  SbBool isInitialized() const;

protected:
  void setInitialized(SbBool state);
  void setInitParams(const SoRenderBackendInitParams & params);
  const SoRenderBackendInitParams & getInitParams() const;

  void emitLog(const char * message) const;
  void emitError(const char * message) const;

  void debugValidateDrawList(const SoDrawList & drawlist) const;

private:
  SbBool                    initialized;
  SoRenderBackendInitParams initParams;
};

#endif // COIN_SORENDERBACKEND_H
