// src/rendering/SoRenderBackend.cpp

#include "rendering/SoRenderBackend.h"

#include <Inventor/rendering/SoRenderIR.h>

#include <Inventor/errors/SoDebugError.h>

#include <cassert>

SoRenderBackend::SoRenderBackend()
  : initialized(FALSE), phaseTimingEnabled(FALSE), initParams()
{
}

SoRenderBackend::~SoRenderBackend()
{
#ifdef COIN_DEBUG
  assert(!this->initialized &&
         "SoRenderBackend destroyed without calling shutdown()");
#endif // COIN_DEBUG
}

void
SoRenderBackend::discard()
{
  this->setInitialized(FALSE);
}
SbBool
SoRenderBackend::updatePickBuffer(const SoDrawList &,
                                   const SoRenderPlan &,
                                   const SoRenderParams &)
{
  return FALSE;
}

SbBool
SoRenderBackend::pickClosest(int, int, int, SoPickResult &)
{
  return FALSE;
}

SbBool
SoRenderBackend::pickClosest(int x, int y, int radius,
                             SoPickReadbackMode mode, SoPickResult & result)
{
  return mode == SoPickReadbackMode::ID_AND_DEPTH
    ? this->pickClosest(x, y, radius, result) : FALSE;
}

SbBool
SoRenderBackend::requestPickClosestAsync(int, int, int, SoAsyncPickRequest &)
{
  return FALSE;
}

SbBool
SoRenderBackend::requestPickClosestAsync(int x, int y, int radius,
                                         SoPickReadbackMode mode,
                                         SoAsyncPickRequest & request)
{
  return mode == SoPickReadbackMode::ID_AND_DEPTH
    ? this->requestPickClosestAsync(x, y, radius, request) : FALSE;
}

SoAsyncPickStatus
SoRenderBackend::pollPickClosestAsync(const SoAsyncPickRequest &,
                                      SoPickResult &)
{
  return SoAsyncPickStatus::FAILED;
}

SbBool
SoRenderBackend::requestPickIdentityAsync(const int x, const int y,
                                          const int radius,
                                          SoAsyncPickRequest & request)
{
  return this->requestPickClosestAsync(
    x, y, radius, SoPickReadbackMode::ID_ONLY, request);
}

SoAsyncPickStatus
SoRenderBackend::pollPickIdentityAsync(const SoAsyncPickRequest & request,
                                       SoPickIdentity & result)
{
  result = SoPickIdentity();
  SoPickResult hit;
  const SoAsyncPickStatus status = this->pollPickClosestAsync(request, hit);
  if (status != SoAsyncPickStatus::HIT) return status;
  if (hit.hasDepth || request.mode != SoPickReadbackMode::ID_ONLY) {
    return SoAsyncPickStatus::FAILED;
  }
  result.generation = hit.generation;
  result.commandIndex = hit.commandIndex;
  result.nodeId = hit.nodeId;
  result.instanceId = hit.instanceId;
  result.objectId = hit.objectId;
  result.type = hit.type;
  result.elementIndex = hit.elementIndex;
  result.pixelX = hit.pixelX;
  result.pixelY = hit.pixelY;
  return SoAsyncPickStatus::HIT;
}

SoRenderStatistics
SoRenderBackend::getRenderStatistics() const
{
  return SoRenderStatistics();
}

void
SoRenderBackend::setPhaseTimingEnabled(const SbBool enabled)
{
  this->phaseTimingEnabled = enabled;
}

SbBool
SoRenderBackend::isPhaseTimingEnabled() const
{
  return this->phaseTimingEnabled;
}

SbBool
SoRenderBackend::pickVisibleRegion(const SbBox2s &,
                                   SoPickResultList &)
{
  return FALSE;
}

SbBool
SoRenderBackend::pickDepthStack(int, int, int, int, int,
                                SoPickResultList &)
{
  return FALSE;
}

SbBool
SoRenderBackend::renderSelection(const SoDrawList &,
                                  const SoSelectionState &,
                                  const SoRenderParams &)
{
  return FALSE;
}

SbBool
SoRenderBackend::isInitialized() const
{
  return this->initialized;
}

void
SoRenderBackend::setInitialized(const SbBool state)
{
  this->initialized = state;
}

void
SoRenderBackend::setInitParams(const SoRenderBackendInitParams & params)
{
  this->initParams = params;
}

const SoRenderBackendInitParams &
SoRenderBackend::getInitParams() const
{
  return this->initParams;
}

void
SoRenderBackend::emitLog(const char * message) const
{
  if (this->initParams.logCallback) {
    this->initParams.logCallback(message, this->initParams.userData);
    return;
  }
  SoDebugError::postInfo("SoRenderBackend", "%s", message);
}

void
SoRenderBackend::emitError(const char * message) const
{
  if (this->initParams.errorCallback) {
    this->initParams.errorCallback(message, this->initParams.userData);
    return;
  }
  SoDebugError::post("SoRenderBackend", "%s", message);
}

void
SoRenderBackend::debugValidateDrawList(const SoDrawList & drawlist) const
{
#ifdef COIN_DEBUG
  const int num = drawlist.getNumCommands();
  for (int i = 0; i < num; ++i) {
    const SoRenderCommand & command = drawlist.getCommand(i);
    const SoGeometryDesc & geometry = drawlist.getCommandGeometry(command);
    if (geometry.topology >= SO_TOPOLOGY_COUNT) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d has invalid topology (%d)",
                         i, static_cast<int>(geometry.topology));
    }
    if (geometry.vertexCount == 0 && geometry.indexCount == 0) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d has no vertices or indices", i);
    }
    if (geometry.vertexCount > 0 && geometry.positions == nullptr &&
        geometry.cacheKey == 0) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d is missing its position buffer", i);
    }
    if (geometry.indexCount > 0 && geometry.indices == nullptr) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d is missing its index buffer", i);
    }
  }
#else
  (void) drawlist;
#endif // COIN_DEBUG
}
