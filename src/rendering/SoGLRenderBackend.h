
#ifndef COIN_SOGLRENDERBACKEND_H
#define COIN_SOGLRENDERBACKEND_H

#include "rendering/SoRenderBackend.h"

#include <Inventor/rendering/SoRenderIR.h>
#include <Inventor/system/gl.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

struct cc_glglue;

/*!
  \class SoGLRenderBackend
  \brief Core-profile OpenGL executor for retained DrawList IR.

  Executable retained shader roots target GLSL 330 core and this backend
  requires OpenGL 3.3 or newer. The roots are deliberately core-safe, but
  the active context may still be a compatibility context while LegacyGL is
  used elsewhere by the application.

  All GL resources are owned by the current backend context and are released
  by shutdown() while that context is current. The backend does not take
  ownership of the scene data referenced by a SoDrawList.

  \ingroup coin_retained_rendering
*/
class COIN_DLL_API SoGLRenderBackend : public SoRenderBackend {
public:
  SoGLRenderBackend();
  ~SoGLRenderBackend() override;

  const char * getName() const override;
  SbBool initialize(const SoRenderBackendInitParams & params) override;
  void shutdown() override;
  SbBool render(const SoDrawList & drawlist,
                const SoRenderPlan & plan,
                const SoRenderParams & params) override;

private:
  struct ResourceCacheKey {
    uint64_t geometry = 0;
    uint64_t texture = 0;
    bool operator==(const ResourceCacheKey & rhs) const
    { return this->geometry == rhs.geometry && this->texture == rhs.texture; }
  };
  struct ResourceCacheKeyHash {
    size_t operator()(const ResourceCacheKey & key) const
    {
      const size_t geometryHash = std::hash<uint64_t>()(key.geometry);
      const size_t textureHash = std::hash<uint64_t>()(key.texture);
      return geometryHash ^ (textureHash + static_cast<size_t>(0x9e3779b9) +
                             (geometryHash << 6) + (geometryHash >> 2));
    }
  };

  struct CachedCommand {
    GLuint positionBuffer = 0;
    GLuint normalBuffer = 0;
    GLuint colorBuffer = 0;
    GLuint texcoordBuffer = 0;
    GLuint texture = 0;
    GLuint indexBuffer = 0;
    GLuint vertexArray = 0;

    const float * positionsKey = nullptr;
    const float * normalsKey = nullptr;
    const float * colorsKey = nullptr;
    const float * texcoordsKey = nullptr;
    const unsigned char * texturePixelsKey = nullptr;
    const uint32_t * indicesKey = nullptr;
    uint32_t vertexCount = 0;
    uint32_t normalCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;
    uint32_t texcoordStride = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    int textureComponents = 0;
    SoTextureColorSpace textureColorSpace = SO_TEXTURE_COLORSPACE_LEGACY;
    SoTextureFilter textureMinFilter = SO_TEXTURE_FILTER_NEAREST;
    SoTextureFilter textureMagFilter = SO_TEXTURE_FILTER_NEAREST;
    SoTextureWrap textureWrapS = SO_TEXTURE_WRAP_CLAMP_TO_EDGE;
    SoTextureWrap textureWrapT = SO_TEXTURE_WRAP_CLAMP_TO_EDGE;
    bool textureAnisotropic = false;
    uint64_t geometryCacheKey = 0;
    uint64_t geometryRevision = 0;
    uint64_t textureCacheKey = 0;
    uint64_t textureRevision = 0;
    ResourceCacheKey resourceKey;
    bool persistent = false;
  };

  struct VisualProgram {
    GLuint handle = 0;

    struct Uniforms {
      struct Transforms {
        GLint view = -1;
        GLint projection = -1;
        GLint model = -1;
      } transforms;
      struct Material {
        GLint color = -1;
        GLint useVertexColor = -1;
        GLint shadingModel = -1;
        GLint emissiveColor = -1;
        GLint ambient = -1;
        GLint specular = -1;
        GLint shininess = -1;
        GLint twoSidedLighting = -1;
        GLint vertexColorAlphaIncludesOpacity = -1;
      } material;
      struct Lighting {
        GLint ambient = -1;
        GLint lightCount = -1;
        GLint lightType = -1;
        GLint lightColor = -1;
        GLint lightDirection = -1;
        GLint lightPosition = -1;
        GLint lightAttenuation = -1;
        GLint lightSpotParams = -1;
      } lighting;
      struct Texture {
        GLint sampler = -1;
        GLint enabled = -1;
        GLint alphaIncludesOpacity = -1;
        GLint hasAlpha = -1;
        GLint model = -1;
        GLint blendColor = -1;
      } texture;
      struct AlphaTest {
        GLint function = -1;
        GLint reference = -1;
      } alphaTest;
    } uniforms;
  } visualProgram;

  bool createShaders();
  bool createVisualProgram();
  const VisualProgram & selectSurfaceProgram(
    const SoRenderCommand & command) const;
  void beginFrame(const SoRenderParams & params);
  void invalidateCache();
  void updateGeometryCache(const SoDrawList & drawlist);
  void drawCommand(const SoDrawList & drawlist,
                   const SoRenderCommand & command,
                   const SbMat & viewMat,
                   const SbMat & projMat,
                   const SoRenderParams & params);
  void uploadLighting(const SoDrawList & drawlist,
                      const SoRenderCommand & command);

  CachedCommand & getOrCreateCache(const SoRenderCommand * command);
  void uploadGeometry(CachedCommand & entry,
                      const SoRenderCommand & command);
  void uploadVertexBuffers(CachedCommand & entry,
                           const SoGeometryDesc & geometry);
  void uploadTexture(CachedCommand & entry,
                     const SoGeometryDesc & geometry,
                     const SoTextureData & texture);
  void uploadIndices(CachedCommand & entry,
                     const SoGeometryDesc & geometry);
  void updateCacheDescription(CachedCommand & entry,
                              const SoRenderCommand & command,
                              bool hasTexture,
                              uint32_t vertexStride);
  void setupVisualVAO(CachedCommand & entry);
  void destroyCacheEntry(CachedCommand & entry);
  void bindVisualCommand(const SoDrawList & drawlist,
                         const SoRenderCommand & command,
                         const CachedCommand & entry,
                         const SbMat & viewMat,
                         const SbMat & projMat,
                         const SoRenderParams & params);
  void bindTransforms(const SoRenderCommand & command,
                      const SbMat & viewMat,
                      const SbMat & projMat);
  void bindMaterial(const SoRenderCommand & command,
                    const CachedCommand & entry);
  void applyDepthState(const SoRenderCommand & command);
  void applyBlendState(const SoRenderCommand & command);
  void bindAlphaTest(const SoRenderCommand & command);
  void bindTexture(const SoRenderCommand & command,
                   const CachedCommand & entry);
  void drawGeometry(const SoRenderCommand & command,
                    const CachedCommand & entry);
  bool textureDescriptionMatches(const CachedCommand & entry,
                                 const SoRenderCommand & command) const;

  const cc_glglue * glue = nullptr;
  std::vector<CachedCommand> gpuCache;
  std::unordered_map<const SoRenderCommand *, size_t> commandToCache;
  std::unordered_map<ResourceCacheKey, size_t, ResourceCacheKeyHash> resourceToCache;
  uint32_t cacheGeneration = 0;
  size_t cachedCommandCount = 0;
  bool haveCacheGeneration = false;
};

#endif // COIN_SOGLRENDERBACKEND_H
