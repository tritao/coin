
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
  void discard() override;
  SbBool render(const SoDrawList & drawlist,
                const SoRenderPlan & plan,
                const SoRenderParams & params,
                const SoSelectionState * selection = nullptr) override;

  //! Render the current DrawList into the explicit integer picking buffer.
  SbBool updatePickBuffer(const SoDrawList & drawlist,
                          const SoRenderPlan & plan,
                          const SoRenderParams & params) override;
  //! Resolve the closest nonzero ID in a viewport-local pixel-radius query.
  SbBool pickClosest(int x, int y, int radius,
                     SoPickResult & result) override;
  SbBool pickClosest(int x, int y, int radius, SoPickReadbackMode mode,
                     SoPickResult & result) override;
  SbBool requestPickClosestAsync(int x, int y, int radius,
                                 SoAsyncPickRequest & request) override;
  SbBool requestPickClosestAsync(int x, int y, int radius,
                                 SoPickReadbackMode mode,
                                 SoAsyncPickRequest & request) override;
  SoAsyncPickStatus pollPickClosestAsync(
    const SoAsyncPickRequest & request, SoPickResult & result) override;
  SoRenderStatistics getRenderStatistics() const override;
  SbBool pickVisibleRegion(const SbBox2s & region,
                           SoPickResultList & results) override;
  SbBool pickDepthStack(int x, int y, int radius, int maxLayers,
                        int maxHits,
                        SoPickResultList & results) override;
  //! Render explicit selected/highlighted targets over the current framebuffer.
  SbBool renderSelection(const SoDrawList & drawlist,
                         const SoSelectionState & selection,
                         const SoRenderParams & params) override;

private:
  enum class InstanceCommandClass {
    ELIGIBLE,
    GEOMETRY,
    VERTEX_ATTRIBUTES,
    MATERIAL,
    TEXTURE,
    RENDER_STATE
  };
  enum class InstanceCompatibility {
    COMPATIBLE,
    COMMAND_INELIGIBLE,
    GEOMETRY_RESOURCE,
    MATERIAL,
    RENDER_STATE
  };

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
    GLuint lineDistanceBuffer = 0;
    GLuint lineRasterVertexArray = 0;
    GLuint lineRasterPositionBuffer = 0;
    GLuint lineRasterNormalBuffer = 0;
    GLuint lineRasterColorBuffer = 0;
    GLuint lineRasterTexcoordBuffer = 0;
    GLuint lineRasterDistanceBuffer = 0;
    GLuint texture = 0;
    GLuint indexBuffer = 0;
    GLuint vertexArray = 0;

    const float * positionsKey = nullptr;
    const float * normalsKey = nullptr;
    const float * colorsKey = nullptr;
    const float * texcoordsKey = nullptr;
    const float * lineDistanceKey = nullptr;
    const unsigned char * texturePixelsKey = nullptr;
    const uint32_t * indicesKey = nullptr;
    const float * lineRasterPositionsKey = nullptr;
    const float * lineRasterNormalsKey = nullptr;
    const float * lineRasterColorsKey = nullptr;
    const float * lineRasterTexcoordsKey = nullptr;
    const uint32_t * lineRasterIndicesKey = nullptr;
    uint32_t vertexCount = 0;
    uint32_t normalCount = 0;
    uint32_t indexCount = 0;
    uint32_t lineRasterVertexCount = 0;
    uint32_t lineRasterIndexCount = 0;
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

  struct SurfaceUniforms {
    struct Transforms {
      GLint view = -1;
      GLint projection = -1;
      GLint model = -1;
      GLint instanced = -1;
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
  };

  struct VisualProgram {
    GLuint handle = 0;
    SurfaceUniforms surface;
  } visualProgram;

  struct LineProgram {
    GLuint handle = 0;
    SurfaceUniforms surface;
    struct RasterUniforms {
      GLint lineWidth = -1;
      GLint viewportSize = -1;
      GLint stipplePattern = -1;
      GLint stippleScale = -1;
      GLint cullBackFaces = -1;
      GLint frontFaceCCW = -1;
    } raster;
  };

  struct PointProgram {
    GLuint handle = 0;
    SurfaceUniforms surface;
    struct RasterUniforms {
      GLint pointSize = -1;
      GLint viewportSize = -1;
      GLint cullBackFaces = -1;
      GLint frontFaceCCW = -1;
    } raster;
  };

  struct PixelProgram {
    GLuint handle = 0;
    struct Uniforms {
      GLint view = -1;
      GLint projection = -1;
      GLint model = -1;
      GLint quadCenter = -1;
      GLint sourceSize = -1;
      GLint rasterSize = -1;
      GLint viewportOrigin = -1;
      GLint viewportSize = -1;
      GLint pixelOrigin = -1;
      GLint texture = -1;
      GLint alphaTestFunction = -1;
      GLint alphaTestReference = -1;
    } uniforms;
  };

  struct RasterPrograms {
    LineProgram line;
    LineProgram triangleLine;
    PointProgram point;
    PointProgram trianglePoint;
    PixelProgram pixel;
    float nativeLineWidthMax = 1.0f;
    float nativePointSizeMax = 1.0f;
  } rasterPrograms;

  struct RasterPath {
    GLenum primitive = GL_TRIANGLES;
    bool textured = false;
    bool pixelRaster = false;
    bool usePointShader = false;
    bool useLineShader = false;
    bool lineTriangleInput = false;
    bool pointTriangleInput = false;
    bool expandedLineStream = false;
    bool linePrimitive = false;
    bool pointPrimitive = false;
    bool filledPrimitive = true;
    float pointSize = 1.0f;
    float lineWidth = 1.0f;
  };

  struct PickProgram {
    GLuint handle = 0;
    struct Uniforms {
      GLint view = -1;
      GLint proj = -1;
      GLint model = -1;
      GLint color = -1;
      GLint useVertexColor = -1;
      GLint vertexColorAlphaIncludesOpacity = -1;
      GLint textureAlphaIncludesOpacity = -1;
      GLint textureHasAlpha = -1;
      GLint textureEnabled = -1;
      GLint textureModel = -1;
      GLint textureBlendColor = -1;
      GLint texture = -1;
      GLint selectionColor = -1;
      GLint alphaTestFunction = -1;
      GLint alphaTestReference = -1;
      GLint pickId = -1;
      GLint instanced = -1;
      GLint primitivePickIds = -1;
      GLint vpSize = -1;
      GLint lineWidth = -1;
      GLint pointSize = -1;
      GLint stipplePattern = -1;
      GLint stippleScale = -1;
      GLint cullBackFaces = -1;
      GLint frontFaceCCW = -1;
      GLint quadCenter = -1;
      GLint sourceSize = -1;
      GLint rasterSize = -1;
      GLint viewportOrigin = -1;
      GLint pixelOrigin = -1;
      GLint texModColor = -1;
      GLint previousDepth = -1;
      GLint peelEnabled = -1;
    } uniforms;
  };

  struct PickPrograms {
    PickProgram visual;
    PickProgram opaqueVisual;
    PickProgram line;
    PickProgram triangleLine;
    PickProgram point;
    PickProgram trianglePoint;
    PickProgram pixel;
  } pickPrograms;

  PickPrograms selectionPrograms;

  struct PickTarget {
    GLuint framebuffer = 0;
    GLuint colorTexture = 0;
    GLuint depthTextures[2] = {0, 0};
    int activeDepth = 0;
    bool peelEnabled = false;
    SbVec2s size;
    std::vector<SoPickLUTEntry> lookup;
    uint32_t generation = 0;
    const SoDrawList * drawlist = nullptr;
    SoRenderPlan plan;
    SoRenderParams params;
    bool ready = false;
  } pickTarget;

  struct AsyncPickSlot {
    GLuint buffer = 0;
    GLsync fence = nullptr;
    uint64_t requestId = 0;
    uint32_t generation = 0;
    int left = 0;
    int bottom = 0;
    int width = 0;
    int height = 0;
    int centerX = 0;
    int centerY = 0;
    bool active = false;
    bool includeDepth = true;
    size_t capacityBytes = 0;
  } asyncPickSlots[3];
  uint64_t nextAsyncPickRequestId = 1;
  size_t nextAsyncPickSlot = 0;

  struct InstanceRecord {
    float model[16];
    float color[4];
    uint32_t pickId;
  };
  struct SelectionInstanceScratch {
    size_t targetIndex = 0;
    uint32_t commandIndex = 0;
    SbColor4f color;
    uint32_t primitiveId = 0;
  };
  struct SelectionBatchScratch {
    std::vector<SelectionInstanceScratch> instances;
    bool primitiveSelection = false;
    uint32_t sourcePrimitiveCount = 0;
  };
  struct SelectionPassScratch {
    std::vector<SelectionBatchScratch> batches;
    std::unordered_map<size_t, std::vector<size_t> > batchesByGeometry;
    size_t batchCount = 0;
    const SoDrawList * drawlist = nullptr;
    uint32_t drawlistGeneration = 0;
    uint64_t commandContentRevision = 0;
    uint64_t selectionRevision = 0;
    size_t targetCount = 0;
    bool cacheValid = false;
  } selectionPasses[2];
  std::vector<InstanceRecord> selectionInstanceRecords;

  struct SubmissionCache {
    // GL state is grouped by the code responsible for changing it. A group is
    // invalidated when rendering temporarily bypasses these helpers.
    struct ProgramState {
      GLuint handle = 0;
      bool viewportValid = false;
      int viewport[4] = {0, 0, 0, 0};
      bool matricesValid = false;
      GLuint matrixProgram = 0;
      SbMat view;
      SbMat projection;
    } program;

    struct MaterialState {
      bool valid = false;
      GLuint program = 0;
      SoMaterialData material;
      SoAlphaTestState alphaTest;
      SoLightingHandle lightingHandle = 0;
      bool useVertexColor = false;
      bool textured = false;
    } material;

    struct PipelineState {
      bool depthValid = false;
      bool depthTest = true;
      GLenum depthFunction = GL_LEQUAL;
      bool depthWrite = true;
      SbVec2f depthRange = SbVec2f(0.0f, 1.0f);

      bool rasterValid = false;
      bool cull = false;
      GLenum frontFace = GL_CCW;
      GLenum polygonMode = GL_FILL;
      float pointSize = 1.0f;
      float lineWidth = 1.0f;

      bool blendValid = false;
      bool blend = false;
      GLenum blendSrcRGB = GL_ONE;
      GLenum blendDstRGB = GL_ZERO;
      GLenum blendSrcAlpha = GL_ONE;
      GLenum blendDstAlpha = GL_ZERO;
      GLenum blendEquation = GL_FUNC_ADD;

      bool textureValid = false;
      GLuint texture = 0;

      bool vertexArrayValid = false;
      GLuint vertexArray = 0;
    } pipeline;

    SoRenderStatistics statistics;
  } submissionCache;

  struct CommandFrame {
    SbMat view;
    SbMat projection;
    SbVec2s viewportOrigin;
    SbVec2s viewportSize;
  };

  struct PickBatch {
    std::vector<uint32_t> commandIndices;
    std::vector<GLuint> pickIds;
    bool primitivePickIds = false;

    bool replacesIndividualDraws() const
    { return primitivePickIds || commandIndices.size() > 1; }
  };

  bool createShaders();
  void clearSelectionScratch();
  const VisualProgram & selectSurfaceProgram(
    const SoRenderCommand & command) const;
  bool ensurePickFramebuffer(const SbVec2s & size);
  void destroyPickFramebuffer();
  void destroyAsyncPickResources(bool releaseGL);
  void drawPickEntry(const SoDrawList & drawlist,
                     const SoPickLUTEntry & entry,
                     GLuint id,
                     const SbMat & viewMat,
                     const SbMat & projMat,
                     const SoRenderParams & params);
  void drawInstancedPickCommands(const SoDrawList & drawlist,
                                 const std::vector<uint32_t> & commandIndices,
                                 const std::vector<GLuint> & pickIds,
                                 const SoRenderParams & params,
                                 bool primitivePickIds = false);
  PickBatch collectPickBatch(
    const SoDrawList & drawlist,
    const std::vector<uint32_t> & candidates,
    size_t firstCandidate,
    const std::vector<SoPickLUTEntry> & lookup,
    const std::vector<std::vector<size_t> > & lookupByCommand) const;
  void drawSelectionEntry(const SoDrawList & drawlist,
                          const SoPickLUTEntry & entry,
                          const SbColor4f & color,
                          const SbMat & viewMat,
                          const SbMat & projMat,
                          const SoRenderParams & params);
  void drawInstancedSelectionCommands(
    const SoDrawList & drawlist,
    const SelectionBatchScratch & batch,
    const SoRenderParams & params,
    bool primitiveSelection);
  void drawCoverageEntry(const SoDrawList & drawlist,
                         const SoPickLUTEntry & entry,
                         GLuint id,
                         const SbColor4f & selectionColor,
                         const SbMat & viewMat,
                         const SbMat & projMat,
                         const SoRenderParams & params,
                         bool selection);
  void beginFrame(const SoRenderParams & params);
  // Restore the externally visible GL defaults once, after all commands.
  void restoreSubmissionBaseline();
  void invalidateSubmissionCache();
  // Keep instrumentation out of the individual state comparisons.
  void recordStateChange(bool changed);
  void bindVertexArray(GLuint vertexArray);
  void useProgram(GLuint program);
  void setViewport(int x, int y, int width, int height);
  void uploadFrameMatrices(GLuint program, const SurfaceUniforms & uniforms,
                           const SbMat & view, const SbMat & projection);
  void invalidateCache();
  void updateGeometryCache(const SoDrawList & drawlist);
  void updateLineDistances(CachedCommand & entry,
                           const SoRenderCommand & command,
                           const SbMat & viewMat,
                           const SbMat & projMat,
                           const SbVec2s & viewportSize);
  void updateIndexedLineRasterStream(CachedCommand & entry,
                                     const SoRenderCommand & command,
                                     const SbMat & viewMat,
                                     const SbMat & projMat,
                                     const SbVec2s & viewportSize);
  void setupLineRasterVAO(CachedCommand & entry);
  void destroyLineRasterStream(CachedCommand & entry);
  CommandFrame effectiveCommandFrame(const SoRenderCommand & command,
                                      const SoRenderParams & params,
                                      bool framebufferLocal) const;
  void clearDepthEvent(const SoDepthClearEvent & event,
                       const SoRenderParams & params,
                       bool framebufferLocal);
  void drawCommand(const SoDrawList & drawlist,
                   const SoRenderCommand & command,
                   const SbMat & viewMat,
                   const SbMat & projMat,
                   const SoRenderParams & params);
  bool canInstanceCommand(const SoRenderCommand & command) const;
  bool canInstanceTogether(const SoRenderCommand & first,
                           const SoRenderCommand & next) const;
  InstanceCommandClass classifyInstanceCommand(
    const SoRenderCommand & command) const;
  InstanceCompatibility classifyInstanceCompatibility(
    const SoRenderCommand & first, const SoRenderCommand & next) const;
  InstanceCompatibility classifyEligibleInstanceCompatibility(
    const SoRenderCommand & first, const SoRenderCommand & next) const;
  void drawInstancedCommands(const SoDrawList & drawlist,
                             const std::vector<uint32_t> & commandIndices,
                             const SoRenderParams & params);
  RasterPath selectRasterPath(const CachedCommand & entry,
                              const SoRenderCommand & command,
                              const SoRenderParams & params) const;
  void applyDepthState(const SoRenderCommand & command);
  void applyRasterState(const SoRenderCommand & command,
                        const RasterPath & path);
  void applyBlendState(const SoRenderCommand & command);
  bool applyPolygonOffset(const SoRenderCommand & command,
                          const RasterPath & path,
                          GLenum & target);
  void bindCommandProgram(const SoDrawList & drawlist,
                          const SoRenderCommand & command,
                          const RasterPath & path,
                          const SbMat & viewMat,
                          const SbMat & projMat,
                          const SbVec2s & viewportOrigin,
                          const SbVec2s & viewportSize,
                          const CachedCommand & entry);
  void drawGeometry(const SoRenderCommand & command,
                    const RasterPath & path,
                    const CachedCommand & entry);
  void restoreRasterState(const RasterPath & path,
                          GLenum polygonOffsetTarget,
                          bool polygonOffsetEnabled);
  void uploadLighting(const SoDrawList & drawlist,
                      const SoRenderCommand & command,
                      const SurfaceUniforms & uniforms);

  CachedCommand & getOrCreateCache(const SoRenderCommand * command,
                                   const SoGeometryDesc & geometry);
  void uploadGeometry(CachedCommand & entry,
                      const SoRenderCommand & command,
                      const SoGeometryDesc & geometry);
  void uploadVertexBuffers(CachedCommand & entry,
                           const SoGeometryDesc & geometry);
  void uploadTexture(CachedCommand & entry,
                     const SoRenderCommand & command);
  void uploadLineDistanceBuffer(CachedCommand & entry,
                                const SoGeometryDesc & geometry,
                                GLsizei vertexStride);
  void uploadIndices(CachedCommand & entry,
                     const SoGeometryDesc & geometry);
  void updateCacheDescription(CachedCommand & entry,
                              const SoRenderCommand & command,
                              const SoGeometryDesc & geometry,
                              GLsizei vertexStride);
  void setupVisualVAO(CachedCommand & entry);
  void destroyCacheEntry(CachedCommand & entry);
  bool textureDescriptionMatches(const CachedCommand & entry,
                                 const SoRenderCommand & command) const;
  void bindRasterCommon(const SoDrawList & drawlist,
                        const SoRenderCommand & command,
                        const SbMat & viewMat,
                        const SbMat & projMat,
                        const SbVec4f & color,
                        bool useVertexColor,
                        bool textured,
                        GLuint program,
                        const SurfaceUniforms & uniforms);
  void bindLineShader(const SoRenderCommand & command,
                      const SbMat & viewMat,
                      const SbMat & projMat,
                      const SbVec4f & color,
                      bool useVertexColor,
                      float lineWidth,
                      const SbVec2s & viewportSize,
                      bool triangleInput,
                      const SoDrawList & drawlist,
                      bool textured);
  void bindPointShader(const SoRenderCommand & command,
                       const SbMat & viewMat,
                       const SbMat & projMat,
                       const SbVec4f & color,
                       bool useVertexColor,
                       float pointSize,
                       const SbVec2s & viewportSize,
                       bool triangleInput,
                       const SoDrawList & drawlist,
                       bool textured);
  void bindPixelShader(const SoRenderCommand & command,
                       const SbMat & viewMat,
                       const SbMat & projMat,
                       const SbVec2s & viewportOrigin,
                       const SbVec2s & viewportSize);

  const cc_glglue * glue = nullptr;
  void * context = nullptr;
  std::vector<CachedCommand> gpuCache;
  std::unordered_map<const SoRenderCommand *, size_t> commandToCache;
  std::unordered_map<ResourceCacheKey, size_t, ResourceCacheKeyHash> resourceToCache;
  uint32_t cacheGeneration = 0;
  size_t cachedCommandCount = 0;
  bool haveCacheGeneration = false;
  GLuint instanceBuffer = 0;
};

#endif // COIN_SOGLRENDERBACKEND_H
