// src/rendering/SoRenderIRP.h

#ifndef COIN_SORENDERIRP_H
#define COIN_SORENDERIRP_H

#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/rendering/SoRenderIR.h>

#include <cstddef>
#include <memory>

class SoState;

/*!
  \class SoIRBuffer
  \brief Chunk-based CPU scratch allocator for per-frame geometry data.

  Allocations are stable: pointers remain valid until clear() is called.
  Growth allocates new chunks without moving old data.
*/
class SoIRBuffer {
public:
  struct Checkpoint {
    std::vector<size_t> cursors;
    size_t chunkCount = 0;
    size_t totalAllocated = 0;
  };

  SoIRBuffer();
  ~SoIRBuffer() = default;

  void clear();
  void reserve(size_t bytes);
  void * allocate(size_t bytes, size_t alignment = alignof(float));
  Checkpoint checkpoint() const;
  void rewind(const Checkpoint & checkpoint);

  template <typename T>
  T * allocateArray(size_t count, size_t alignment = alignof(T)) {
    return static_cast<T *>(this->allocate(count * sizeof(T), alignment));
  }

  size_t size() const { return this->totalAllocated; }

private:
  static constexpr size_t MIN_CHUNK_SIZE = 1024 * 1024; // 1 MB
  struct Chunk {
    std::vector<uint8_t> data;
    size_t cursor = 0;
  };
  std::vector<std::unique_ptr<Chunk>> chunks;
  size_t totalAllocated = 0;
  size_t highWaterMark = 0;  // largest total allocation seen across frames
};

//! Dump a compact summary of the draw list to Coin's debug output.
void SoIRDumpSummary(const SoDrawList & drawlist);
//! Dump the first \a count render commands to Coin's debug output.
void SoIRDumpFirstN(const SoDrawList & drawlist, int count);
//! Return whether render-backend trace logging is enabled.
SbBool coin_render_ir_trace_enabled();

/*!
  \namespace SoRenderIR
  \brief Helper functions for converting Coin state and caches into render IR.
*/
namespace SoRenderIR {
// Source-private state capture helpers. These are implementation seams for
// retained traversal and are intentionally not part of the installed API.
void captureLightingFromState(SoState * state, SoLightingData & lighting);
void captureRenderContextFromState(SoState * state,
                                   SoIRRenderContext & context);
void applyRenderContextToState(SoState * state,
                               const SoIRRenderContext & context);
//! Capture matrices, render state, and lighting shared by retained producers.
void fillCommandTraversalStateFromAction(SoIRRenderAction * action,
                                         SoRenderCommand & command);
//! Capture ordinary shape state, including material and inherited texture.
void fillCommandStateFromAction(SoIRRenderAction * action,
                                SoRenderCommand & command,
                                int materialIndex = 0);
//! Fill a material snapshot from the current Inventor traversal state.
void fillMaterialFromState(SoState * state, SoMaterialData & material,
                           int materialIndex = 0);
//! Fill render-state fields from the current Inventor traversal state.
void fillRenderStateFromState(SoState * state, SoRenderState & renderState);
//! Complete blend state after material opacity has been captured.
void ensureMaterialBlendState(SoRenderState & renderState,
                              const SoMaterialData & material);
//! Extract the current lighting setup, append/deduplicate it, and return its handle.
SoLightingHandle fillLightingFromState(SoState * state, SoDrawList & drawlist);
//! Append a previously captured lighting setup and return its handle.
SoLightingHandle fillLightingFromState(SoState * state, SoDrawList & drawlist,
                                       const SoLightingData & lighting);
//! Return whether the material should be treated as translucent.
bool isMaterialTransparent(const SoMaterialData & material);
//! Complete derived command state after producer-specific adjustments.
void finalizeCommand(SoRenderCommand & command);
}

#endif // COIN_SORENDERIRP_H
