// src/rendering/SoModernIR.h

#ifndef COIN_SOMODERNIR_H
#define COIN_SOMODERNIR_H

#include <Inventor/SbLinear.h>
#include <Inventor/lists/SbList.h>

typedef uint32_t SoGeometryHandle;
typedef uint32_t SoMaterialHandle;
typedef uint32_t SoPipelineHandle;

enum SoRenderPassType {
  SO_RENDERPASS_OPAQUE,
  SO_RENDERPASS_TRANSPARENT,
  SO_RENDERPASS_OVERLAY
};

struct SoRenderState {
  // You can expand this later (depth, blend, raster, etc.)
  SbBool depthTestEnabled;
  SbBool depthWriteEnabled;
  SbBool blendingEnabled;
  // packed bits for sort key, etc.
  uint32_t stateKey;
};

struct SoRenderCommand {
  SoGeometryHandle geometry;
  SoMaterialHandle material;
  SoPipelineHandle pipeline;

  SbMatrix        modelMatrix;
  SoRenderState   state;

  SoRenderPassType pass;
  uint64_t         sortKey;       // for backend sorting
  void *           userData;      // optional link back to node/path
};

class SoDrawList {
public:
  void clear() { this->commands.truncate(0); }
  void addCommand(const SoRenderCommand & cmd) { this->commands.append(cmd); }

  int getNumCommands() const { return this->commands.getLength(); }
  const SoRenderCommand & getCommand(int i) const { return this->commands[i]; }

private:
  SbList<SoRenderCommand> commands;
};

#endif // COIN_SOMODERNIR_H
