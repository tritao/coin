// src/rendering/SoRenderBackend.cpp

#include "rendering/SoRenderBackend.h"

#include <Inventor/rendering/SoRenderIR.h>

#include <Inventor/errors/SoDebugError.h>

#include <cassert>

SoRenderBackend::SoRenderBackend()
  : initialized(FALSE), initParams()
{
}

SoRenderBackend::~SoRenderBackend()
{
#ifdef COIN_DEBUG
  assert(!this->initialized &&
         "SoRenderBackend destroyed without calling shutdown()");
#endif // COIN_DEBUG
}

SbBool
SoRenderBackend::updatePickBuffer(const SoDrawList &,
                                   const SoRenderPlan &,
                                   const SoRenderParams &)
{
  return FALSE;
}

SbBool
SoRenderBackend::pick(int, int, int, SoPickResult &) const
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
    if (command.geometry.topology >= SO_TOPOLOGY_COUNT) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d has invalid topology (%d)",
                         i, static_cast<int>(command.geometry.topology));
    }
    if (command.geometry.vertexCount == 0 &&
        command.geometry.indexCount == 0) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d has no vertices or indices", i);
    }
    if (command.geometry.vertexCount > 0 &&
        command.geometry.positions == nullptr &&
        command.geometry.cacheKey == 0) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d is missing its position buffer", i);
    }
    if (command.geometry.indexCount > 0 &&
        command.geometry.indices == nullptr) {
      SoDebugError::post("SoRenderBackend",
                         "Command %d is missing its index buffer", i);
    }
  }
#else
  (void) drawlist;
#endif // COIN_DEBUG
}
