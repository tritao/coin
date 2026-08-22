// include/Inventor/rendering/SoRenderIR.h

#ifndef COIN_SORENDERIR_H
#define COIN_SORENDERIR_H

#include <Inventor/SbBasic.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbVec4f.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class SoState;
class SoNode;

/*!
  \file SoRenderIR.h
  \brief Backend-neutral intermediate representation for retained rendering.

  SoIRRenderAction produces a SoDrawList while traversing a scene graph. A
  renderer backend consumes that list to produce pixels or another
  backend-specific result. The types in this file deliberately use
  semantic values instead of OpenGL enums so the intermediate representation
  does not require a particular graphics API.

  Geometry and embedded texture pointers are borrowed from the producer. They
  normally refer to storage owned by the current SoIRRenderAction frame and
  must not be retained after that frame is cleared, rewound, or replaced.
  Device objects, caches, and other implementation resources belong to the
  consumer, not to the intermediate representation.
*/

/*!
  \defgroup coin_retained_rendering Retained Rendering
  \brief Backend-neutral retained rendering and execution interfaces.

  The retained path records scene semantics in an intermediate representation,
  then lets a manager and a concrete backend decide when and how to execute
  the recorded frame.
*/

/*!
  \enum SoPrimitiveTopology
  \brief Enumerates how primitives referenced by a geometry buffer should be interpreted.
*/
enum SoPrimitiveTopology : uint8_t {
  SO_TOPOLOGY_TRIANGLES = 0,
  SO_TOPOLOGY_LINES,
  SO_TOPOLOGY_POINTS,
  SO_TOPOLOGY_TRIANGLE_STRIP,
  SO_TOPOLOGY_LINE_STRIP,
  SO_TOPOLOGY_COUNT
};

/*!
  \struct SoGeometryDesc
  \brief Describes vertex/index data for a single draw call.

  All pointers remain owned by the producer (typically SoIRRenderAction).
  They must remain valid while the backend consumes the frame. They may point
  into the action's frame geometry pool and must not be retained after that
  storage is cleared or rewound. Backends are free to copy the data into
  backend-owned buffers.

  Strides are byte distances between successive entries. A zero position or
  normal stride means three tightly packed floats; a zero texture-coordinate
  stride means four tightly packed floats. normalCount may be smaller than
  vertexCount when only part of a geometry has normals.
*/
struct SoGeometryDesc {
  SoPrimitiveTopology topology = SO_TOPOLOGY_TRIANGLES;
  uint32_t            vertexCount = 0;
  uint32_t            normalCount = 0;
  uint32_t            indexCount = 0;

  const float *       positions = nullptr;
  const float *       normals = nullptr;
  const float *       texcoords = nullptr;
  const float *       colors = nullptr;
  const uint32_t *    indices = nullptr;

  uint32_t            vertexStride = 0;   //!< Position/normal stride in bytes.
  uint32_t            texcoordStride = 0; //!< Texture-coordinate stride in bytes.

  // A nonzero key identifies the source geometry across retained frames.
  // Revision changes invalidate the corresponding backend resource. A zero
  // key keeps the existing frame-local lifetime contract.
  uint64_t            cacheKey = 0;
  uint64_t            revision = 0;

  // Cheap local-space bounds retained for planning. Producers may leave this
  // unset when the backend should use its conservative origin fallback.
  SbVec3f             boundsCenter = SbVec3f(0.0f, 0.0f, 0.0f);
  SbBool              hasBounds = FALSE;

};

//! Stable, draw-list-local reference to a geometry resource.
using SoGeometryHandle = uint32_t;
static constexpr SoGeometryHandle SO_INVALID_GEOMETRY_HANDLE = 0;

//! Backend-neutral identity kinds retained for picking.
enum SoPickElementType : uint8_t {
  SO_PICK_OBJECT = 0,
  SO_PICK_FACE,
  SO_PICK_EDGE,
  SO_PICK_VERTEX
};

//! Maps one logical subelement to a geometry draw range.
struct SoRenderElementRange {
  SoPickElementType type = SO_PICK_OBJECT;
  int elementIndex = -1;
  uint32_t drawStart = 0;
  uint32_t drawCount = 0;
};

/*!
  \struct SoGeometryResource
  \brief Draw-list-owned geometry descriptor with producer identity.

  Handles are one-based and remain stable until SoDrawList::clear(). The
  descriptor keeps the existing frame-lifetime pointer contract; the resource
  table separates shared geometry identity from individual draw commands.
*/
struct SoGeometryResource {
  SoGeometryDesc geometry;
  uint64_t sourceKey = 0;
  uint64_t revision = 0;
  std::vector<SoRenderElementRange> elementRanges;
};

/*!
  \enum SoShadingModel
  \brief Effective shading contract carried by a render command.

  The legacy-compatible model is the current default. It preserves the
  fixed-function Coin/GL behavior while the DrawList backend is migrated to
  an explicit shading model.
*/
enum SoShadingModel : uint8_t {
  SO_SHADING_UNLIT = 0,
  SO_SHADING_LEGACY_GOURAUD
};

// --- Texture sampler state ---
// These are semantic sampler modes rather than OpenGL enum values so the IR
// can be consumed by non-OpenGL backends as well.
enum SoTextureFilter : uint8_t {
  SO_TEXTURE_FILTER_NEAREST = 0,
  SO_TEXTURE_FILTER_LINEAR,
  SO_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST,
  SO_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST,
  SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR,
  SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR
};

enum SoTextureWrap : uint8_t {
  SO_TEXTURE_WRAP_CLAMP_TO_EDGE = 0,
  SO_TEXTURE_WRAP_REPEAT,
  SO_TEXTURE_WRAP_CLAMP_TO_BORDER
};

// Texture environment models retained from SoMultiTextureImageElement. These
// are semantic values; a backend maps them to its own texture-combine API.
enum SoTextureModel : uint8_t {
  SO_TEXTURE_MODEL_MODULATE = 0,
  SO_TEXTURE_MODEL_DECAL,
  SO_TEXTURE_MODEL_BLEND,
  SO_TEXTURE_MODEL_REPLACE
};

// --- Depth state ---------------------------------------------------------

// Semantic comparison functions. These deliberately do not use GL enum
// values: the IR is also consumed by backends which do not share GL's enum
// space.
enum SoDepthFunction : uint8_t {
  SO_DEPTH_NEVER = 0,
  SO_DEPTH_ALWAYS,
  SO_DEPTH_LESS,
  SO_DEPTH_LEQUAL,
  SO_DEPTH_EQUAL,
  SO_DEPTH_GEQUAL,
  SO_DEPTH_GREATER,
  SO_DEPTH_NOTEQUAL
};

// --- Blend state ---------------------------------------------------------

enum SoBlendFactor : uint8_t {
  SO_BLEND_FACTOR_ZERO = 0,
  SO_BLEND_FACTOR_ONE,
  SO_BLEND_FACTOR_SRC_COLOR,
  SO_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
  SO_BLEND_FACTOR_DST_COLOR,
  SO_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
  SO_BLEND_FACTOR_SRC_ALPHA,
  SO_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
  SO_BLEND_FACTOR_DST_ALPHA,
  SO_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
  SO_BLEND_FACTOR_CONSTANT_COLOR,
  SO_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
  SO_BLEND_FACTOR_CONSTANT_ALPHA,
  SO_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
  SO_BLEND_FACTOR_SRC_ALPHA_SATURATE,
  SO_BLEND_FACTOR_SRC1_COLOR,
  SO_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
  SO_BLEND_FACTOR_SRC1_ALPHA,
  SO_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA
};

enum SoBlendEquation : uint8_t {
  SO_BLEND_EQUATION_ADD = 0,
  SO_BLEND_EQUATION_SUBTRACT,
  SO_BLEND_EQUATION_REVERSE_SUBTRACT,
  SO_BLEND_EQUATION_MIN,
  SO_BLEND_EQUATION_MAX
};

// --- Alpha-test policy --------------------------------------------------

enum SoAlphaTestFunction : uint8_t {
  SO_ALPHA_TEST_NONE = 0,
  SO_ALPHA_TEST_NEVER,
  SO_ALPHA_TEST_ALWAYS,
  SO_ALPHA_TEST_LESS,
  SO_ALPHA_TEST_LEQUAL,
  SO_ALPHA_TEST_EQUAL,
  SO_ALPHA_TEST_GEQUAL,
  SO_ALPHA_TEST_GREATER,
  SO_ALPHA_TEST_NOTEQUAL
};

enum SoAlphaTestPolicy : uint8_t {
  SO_ALPHA_TEST_POLICY_NONE = 0,
  SO_ALPHA_TEST_POLICY_EXPLICIT,
  SO_ALPHA_TEST_POLICY_LEGACY_THRESHOLD,
  SO_ALPHA_TEST_POLICY_PRESERVE_EDGES
};

// --- Render param flags (SoRenderParams::flags) ---
static constexpr uint32_t SO_PARAM_CLEAR_WINDOW = 1u;
static constexpr uint32_t SO_PARAM_CLEAR_DEPTH  = 4u;  //!< Clear depth buffer before rendering
//! The owning caller guarantees that this exact draw list is unchanged.
static constexpr uint32_t SO_PARAM_REUSE_DRAW_LIST = 8u;

/*!
  \struct SoTextureData
  \brief Embedded texture payload carried directly by a render command.

  This is used for commands that provide their own embedded image data.
  The memory is owned by the producer of the draw list and must remain valid
  until the backend finishes consuming the frame.
*/
struct SoTextureData {
  const unsigned char * pixels = nullptr;
  int width = 0;
  int height = 0;
  int numComponents = 0; // 1=L, 2=LA, 3=RGB, 4=RGBA
  // True when at least one texel can contribute alpha below one.
  bool hasTransparency = false;

  // A nonzero key permits a backend to retain the texture resource across
  // frame lifetimes. revision changes require the resource contents to be
  // refreshed; zero remains transient.
  uint64_t cacheKey = 0;
  uint64_t revision = 0;

  SoTextureFilter minFilter = SO_TEXTURE_FILTER_NEAREST;
  SoTextureFilter magFilter = SO_TEXTURE_FILTER_NEAREST;
  SoTextureWrap wrapS = SO_TEXTURE_WRAP_CLAMP_TO_EDGE;
  SoTextureWrap wrapT = SO_TEXTURE_WRAP_CLAMP_TO_EDGE;
  // Request anisotropic filtering when Coin's texture-quality policy enables
  // it. The executor selects the driver's supported level.
  bool anisotropic = false;
  SoTextureModel model = SO_TEXTURE_MODEL_MODULATE;
  SbVec4f blendColor = SbVec4f(0.0f, 0.0f, 0.0f, 1.0f);
};

struct SoPixelRasterData {
  // Pixel rasterization is a semantic command property, not a producer
  // identity. The texture retains the source dimensions; these dimensions
  // describe the desired on-screen footprint.
  SbBool enabled = FALSE;
  float originX = 0.0f;
  float originY = 0.0f;
  int width = 0;
  int height = 0;
};

/*!
  \struct SoMaterialData
  \brief Resolved material payload for one draw call.

  This is the resolved state required to reproduce today's retained Inventor
  rendering semantics. It is not a universal material schema: future material
  work should evolve the abstraction into resolved material resources rather
  than append every authoring model's parameters to this command payload.
  Texture pixels are embedded as borrowed data; the producer owns the storage
  and keeps it alive until the backend finishes consuming the frame.
*/
struct SoMaterialData {
  SbVec4f  diffuse = {0.8f, 0.8f, 0.8f, 1.0f};
  SbVec4f  ambient = {0.2f, 0.2f, 0.2f, 1.0f};
  SbVec4f  specular = {0.0f, 0.0f, 0.0f, 1.0f};
  SbVec4f  emissive = {0.0f, 0.0f, 0.0f, 1.0f};
  // A command with no retained lighting setup is explicitly unlit. Scene
  // traversal fills this with the effective Gouraud model when lighting is
  // present, so the executor never needs to invent a headlight.
  SoShadingModel shadingModel = SO_SHADING_UNLIT;
  float    shininess = 0.2f;
  float    opacity = 1.0f;

  SoTextureData texture;  //!< Embedded texture data.

  // Some CPU-rasterized textures already multiply their texel alpha by
  // material opacity. Consumers use this to avoid multiplying that opacity a
  // second time while still composing vertex and texture alpha.
  bool     textureAlphaIncludesOpacity = false;

  // Material-derived per-vertex colors can already carry the effective
  // material transparency (for example SoMaterial PER_FACE colors). Packed
  // SoVertexProperty colors carry independent vertex alpha instead.
  bool     vertexColorAlphaIncludesOpacity = false;

  bool     twoSidedLighting = false;
};

/*!
  \struct SoDepthState
  \brief Depth-test configuration for a draw call.
*/
struct SoDepthState {
  SbBool  enabled = TRUE;
  SbBool  writeEnabled = TRUE;
  SoDepthFunction func = SO_DEPTH_LEQUAL;
  SbVec2f range = SbVec2f(0.0f, 1.0f);
};

/*!
  \struct SoBlendState
  \brief Backend-neutral blending configuration.
*/
struct SoBlendState {
  SbBool  enabled = FALSE;
  SoBlendFactor srcRGBFactor = SO_BLEND_FACTOR_ONE;
  SoBlendFactor dstRGBFactor = SO_BLEND_FACTOR_ZERO;
  SoBlendFactor srcAlphaFactor = SO_BLEND_FACTOR_ONE;
  SoBlendFactor dstAlphaFactor = SO_BLEND_FACTOR_ZERO;

  // Coin's current LegacyGL state API exposes blend factors but not blend
  // equations. ADD is therefore the only equation that can be captured
  // from traversal today; separate fields keep the IR ready for a future
  // state source without pretending that it is currently preserved.
  SoBlendEquation rgbEquation = SO_BLEND_EQUATION_ADD;
  SoBlendEquation alphaEquation = SO_BLEND_EQUATION_ADD;
};

/*!
  \struct SoAlphaTestState
  \brief Explicit fragment alpha policy for a render command.
*/
struct SoAlphaTestState {
  SoAlphaTestPolicy policy = SO_ALPHA_TEST_POLICY_NONE;
  SoAlphaTestFunction function = SO_ALPHA_TEST_NONE;
  float reference = 0.5f;
};

/*!
  \enum SoRasterFillMode
  \brief Backend-neutral polygon fill mode retained from traversal.
*/
enum SoRasterFillMode : uint8_t {
  SO_RASTER_FILL = 0,
  SO_RASTER_LINES,
  SO_RASTER_POINTS
};

/*!
  \struct SoRasterState
  \brief Rasterizer properties (fill mode, culling, polygon offset).
*/
struct SoRasterState {
  SbBool  visible = TRUE;
  SoRasterFillMode fillMode = SO_RASTER_FILL;
  SbBool  cullBackFaces = FALSE;
  SbBool  frontFaceCCW = TRUE;
  SbBool  scissorEnabled = FALSE;
  SbBool  viewportOverride = FALSE;
  SbBool  viewportEnabled = FALSE;
  int     viewportX = 0;
  int     viewportY = 0;
  int     viewportWidth = 0;
  int     viewportHeight = 0;
  float   lineWidth = 1.0f;
  float   pointSize = 1.0f;
  uint16_t linePattern = 0xFFFF;
  int16_t  linePatternScale = 1;
  float   polygonOffsetFactor = 0.0f;
  float   polygonOffsetUnits = 0.0f;
  SbBool  polygonOffsetFilled = FALSE;
  SbBool  polygonOffsetLines = FALSE;
  SbBool  polygonOffsetPoints = FALSE;
};

/*!
  \struct SoRenderState
  \brief Aggregates depth, blend, alpha-test, and raster state.
*/
struct SoRenderState {
  SoDepthState depth;
  SoBlendState blend;
  SoAlphaTestState alphaTest;
  SoRasterState raster;
  //! Use the view/projection matrices captured with the command.
  SbBool useCommandMatrices = FALSE;
};

/*!
  \enum SoRenderStage
  \brief Ordered scene stage containing draw commands.

*/
enum class SoRenderStage : uint8_t {
  Background,
  Main,
  AfterMain,
  Foreground
};

/*!
  \enum SoOpacityClass
  \brief Semantic surface opacity classification used by the planner.

  This describes whether a command contributes translucent surface semantics.
  It is not an execution pass and does not encode ordering.
*/
enum SoOpacityClass : uint8_t {
  SO_OPACITY_OPAQUE = 0,
  SO_OPACITY_TRANSPARENT
};

/*!
  \struct SoDepthClearEvent
  \brief Explicit depth-buffer clear barrier recorded during traversal.

  The sequence number is a traversal-order barrier. Drawables may be sorted
  within a segment, but execution must never move a command across this
  event. An event is meaningful even when the surrounding group emits no
  drawable command.
*/
struct SoDepthClearEvent {
  SoRenderStage stage = SoRenderStage::Main;
  uint32_t sequence = 0;
  SbBool viewportOverride = FALSE;
  int viewportX = 0;
  int viewportY = 0;
  int viewportWidth = 0;
  int viewportHeight = 0;
};

/*!
  \typedef SoLightingHandle
  \brief Stable 1-based handle into the draw list's deduplicated lighting table.
*/
typedef uint32_t SoLightingHandle;

/*!
  \enum SoLightType
  \brief Light kinds captured in render-backend lighting setups.
*/
enum SoLightType : uint8_t {
  SO_LIGHT_DIRECTIONAL = 0,
  SO_LIGHT_POINT,
  SO_LIGHT_SPOT
};

/*!
  \struct SoLightData
  \brief View-space light description for resolved legacy lighting.

  View space is part of today's LegacyInventor payload, not a permanent
  requirement for future light resources or render passes.
*/
struct SoLightData {
  SoLightType type = SO_LIGHT_DIRECTIONAL;
  SbVec3f     color = SbVec3f(1.0f, 1.0f, 1.0f);
  SbVec3f     direction = SbVec3f(0.0f, 0.0f, 1.0f);
  SbVec3f     position = SbVec3f(0.0f, 0.0f, 1.0f);
  SbVec3f     attenuation = SbVec3f(0.0f, 0.0f, 1.0f);
  float       spotCutoffCos = -1.0f;
  float       spotExponent = 0.0f;
};

/*!
  \struct SoLightingData
  \brief Resolved LegacyInventor lighting setup referenced by render commands.

  ambient preserves Coin's legacy ambient-light semantics. It is not an
  environment-lighting or image-based-lighting representation.
*/
struct SoLightingData {
  SbVec3f ambient = SbVec3f(0.2f, 0.2f, 0.2f);
  std::vector<SoLightData> lights;
};

/*!
  \struct SoIRRenderContext
  \brief State that must survive when a path is replayed after traversal.

  A delayed path is traversed after the original scene traversal has moved on.
  These values preserve the camera, model, and lighting context that cannot
  reliably be reconstructed when a delayed path is replayed. A full path
  replay reconstructs its model state from the path, so callers can suppress
  the model transform when applying the context. The validity flags allow the
  same delayed path element to remain usable by actions which do not enable
  every IR state element.
*/
struct COIN_DLL_API SoIRRenderContext {
  SoLightingData lighting;
  SbMatrix modelMatrix;
  SbViewportRegion viewport;
  SbViewVolume viewVolume;
  SbMatrix viewingMatrix;
  SbMatrix projectionMatrix;
  float devicePixelRatio = 1.0f;
  SbBool hasLighting = FALSE;
  SbBool hasModelMatrix = FALSE;
  SbBool hasViewport = FALSE;
  SbBool hasViewVolume = FALSE;
  SbBool hasViewingMatrix = FALSE;
  SbBool hasProjectionMatrix = FALSE;
  SbBool hasDevicePixelRatio = FALSE;

  // This is a replay supplement, not a complete SoState snapshot. Material,
  // texture, draw-style, pick-style, and other inherited state are rebuilt by
  // traversing the retained path; these fields preserve frame/view state that
  // cannot safely be reconstructed after the original traversal.
  //! Capture the replay-relevant state enabled on an Inventor traversal.
  void captureFromState(SoState * state);
  //! Apply the captured state to an active Inventor traversal.
  void applyToState(SoState * state, SbBool applyModelMatrix = TRUE) const;
};

/*! \struct SoPickData
  \brief Backend-neutral pickability and optional subelement ranges.
*/
struct SoPickData {
  bool pickable = true;
  bool useResourceElementRanges = false;
  std::vector<SoRenderElementRange> elementRanges;
};

/*!
  \struct SoPickLUTEntry
  \brief Frame-local mapping from an integer pick ID to a command range.
*/
struct SoPickLUTEntry {
  int commandIndex = -1;
  uint64_t objectId = 0;
  SoPickElementType type = SO_PICK_OBJECT;
  int elementIndex = -1;
  uint32_t drawStart = 0;
  uint32_t drawCount = 0;
};

/*!
  \struct SoPickResult
  \brief Result of resolving one frame-local integer pick ID.

  The command index is resolved by the backend-neutral DrawList.  A caller
  can use the corresponding SoIRRenderAction to obtain the retained scene
  path for that command; Coin does not encode application identity strings in
  the renderer.

  \ingroup coin_retained_rendering
*/
struct SoPickResult {
  uint32_t id = 0;
  uint32_t generation = 0;
  int commandIndex = -1;
  uint64_t objectId = 0;
  SoPickElementType type = SO_PICK_OBJECT;
  int elementIndex = -1;
  int pixelX = 0;
  int pixelY = 0;
  float depth = 1.0f;
};

/*!
  \struct SoPickResultList
  \brief Results from one frame-local retained picking query.

  Results are renderer mechanics and remain valid only for the recorded
  DrawList generation.  Public scene-graph APIs resolve them to
  SoPickedPoint instances before returning them to applications.
*/
struct SoPickResultList {
  uint32_t generation = 0;
  std::vector<SoPickResult> hits;
  SbBool truncated = FALSE;
};

/*!
  \struct SoSelectionTarget
  \brief One retained command or subelement to highlight in a frame.

  Selection is interaction state applied to retained identity.  It is not
  part of a command's scene identity and carries no application-specific
  names or interaction policy.

  \ingroup coin_retained_rendering
*/
struct SoSelectionTarget {
  int commandIndex = -1;
  uint64_t objectId = 0;
  SoPickElementType type = SO_PICK_OBJECT;
  int elementIndex = -1;
  SbColor4f color = SbColor4f(1.0f, 1.0f, 0.0f, 0.75f);
};

/*!
  \struct SoSelectionState
  \brief Frame-level selected and highlighted retained targets.
*/
struct SoSelectionState {
  std::vector<SoSelectionTarget> selected;
  std::vector<SoSelectionTarget> highlighted;
};

/*!
  \struct SoRenderCommand
  \brief Backend-neutral retained rendering command.

  A command contains the geometry, material, raster state, transforms,
  stage needed to execute one retained draw operation.
  Pointer-valued data is borrowed from storage owned by the producing
  SoDrawList/SoIRRenderAction frame and must not outlive that frame.

  \ingroup coin_retained_rendering
*/
struct SoRenderCommand {
  // Geometry, texture pixels, and other pointer-valued fields are borrowed;
  // see the lifetime contract on SoGeometryDesc and SoTextureData.
  SoGeometryDesc   geometry;
  SoGeometryHandle geometryHandle = SO_INVALID_GEOMETRY_HANDLE;
  SoMaterialData   material;
  SoRenderState    state;

  SbMatrix         modelMatrix;  // default-constructed to identity
  SbMatrix         viewMatrix;
  SbMatrix         projMatrix;

  SoOpacityClass   opacityClass = SO_OPACITY_OPAQUE;
  SoRenderStage    stage = SoRenderStage::Main;
  // Stable scene identity. Zero means that the producer did not provide one.
  uint64_t         objectId = 0;
  SoLightingHandle lightingHandle = 0;
  SoPixelRasterData pixelRaster;
  SoPickData       pick;
  void *           userData = nullptr; //!< Opaque, non-owned producer data.
};

/*!
  \class SoDrawList
  \brief Container holding the commands and auxiliary tables for one frame.

  Commands retain their insertion order. The draw list never imposes
  execution ordering on a backend. clear() starts a new frame and invalidates pointers
  The internal SoRenderPlanner resolves execution order into a separate
  SoRenderPlan; it never reorders the command vector.
  clear() starts a new frame and invalidates pointers
  into producer-owned frame storage.

  Command indices are therefore stable until the list is truncated or
  cleared. Derived lookup tables are frame-local and must be rebuilt after
  their source commands or frame generation changes.

  \ingroup coin_retained_rendering
*/
class COIN_DLL_API SoDrawList {
public:
  SoDrawList();

  //! Clear commands and per-frame tables, beginning a new frame generation.
  void clear();
  void reserve(int count);

  //! Return the generation number incremented when clear() starts a new frame.
  uint32_t getGeneration() const { return generation; }

  void addCommand(const SoRenderCommand & cmd);
  SoRenderCommand & emplaceCommand();

  //! Append a geometry resource and return its stable one-based handle.
  SoGeometryHandle addGeometryResource(const SoGeometryResource & resource);
  //! Return NULL for an invalid handle or a handle outside this draw list.
  SoGeometryResource * getGeometryResource(SoGeometryHandle handle);
  const SoGeometryResource * getGeometryResource(
    SoGeometryHandle handle) const;
  int getNumGeometryResources() const;
  //! Resolve a command resource, falling back to its embedded descriptor.
  const SoGeometryDesc & getCommandGeometry(
    const SoRenderCommand & command) const;
  //! Resolve command-local or shared geometry subelement ranges.
  const std::vector<SoRenderElementRange> & getCommandElementRanges(
    const SoRenderCommand & command) const;

  int getNumCommands() const;
  //! Remove commands beyond index count without reordering remaining commands.
  void truncate(int count);
  //! Mutable access invalidates the frame-local pick lookup table.
  SoRenderCommand & getCommand(int i);
  const SoRenderCommand & getCommand(int i) const;

  //! Add or reuse a lighting setup and return its stable 1-based handle.
  SoLightingHandle addLightingSetup(const SoLightingData & lighting);

  //! Resolve a lighting handle previously returned by addLightingSetup().
  //! Returns NULL for handle 0 or an invalid handle.
  const SoLightingData * getLighting(SoLightingHandle handle) const;

  //! Mutable iteration invalidates the frame-local pick lookup table.
  SoRenderCommand * begin();
  SoRenderCommand * end();
  const SoRenderCommand * begin() const;
  const SoRenderCommand * end() const;

  //! Record an explicit depth-clear barrier at the current insertion point.
  void addDepthClearEvent(const SoDepthClearEvent & event);
  //! Return depth-clear barriers in traversal order.
  const std::vector<SoDepthClearEvent> & getDepthClearEvents() const
  { return this->depthClearEvents; }

  //! Build the frame-local 1-based pick-ID lookup table.
  void buildPickLUT() const;
  //! Resolve a nonzero pick ID, or return NULL for an invalid/stale ID.
  const SoPickLUTEntry * resolvePickId(uint32_t id) const;
  //! Return the generation for which the current pick table was built.
  uint32_t getPickLUTGeneration() const { return pickLUTGeneration; }
  //! Return the immutable snapshot of the current frame's pick table.
  const std::vector<SoPickLUTEntry> & getPickLUT() const { return pickLUT; }

  //! Return frame-local producer selection targets for this draw list.
  //!
  //! These targets are transient traversal output, not persistent manager
  //! identity. The manager combines them with its resolved selection state
  //! immediately before backend execution.
  SoSelectionState & getMutableSelectionState() { return selection; }
  const SoSelectionState & getSelectionState() const { return selection; }

private:
  std::vector<SoRenderCommand> commands;
  std::vector<SoGeometryResource> geometryResources;
  std::vector<SoLightingData> lightingSetups;
  std::vector<SoDepthClearEvent> depthClearEvents;
  SoSelectionState selection;
  mutable std::vector<SoPickLUTEntry> pickLUT;
  uint32_t generation = 0;
  mutable uint32_t pickLUTGeneration = 0;
};

#endif // COIN_SORENDERIR_H
