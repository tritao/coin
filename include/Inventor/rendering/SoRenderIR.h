// include/Inventor/rendering/SoRenderIR.h

#ifndef COIN_SORENDERIR_H
#define COIN_SORENDERIR_H

#include <Inventor/SbBasic.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbVec4f.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class SoGLShaderProgram;

class SoVBO;
class SoVAO;
class SoVertexLayout;

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
  They must stay valid until the backend’s render() call completes. Backends
  are free to copy the data into GPU buffers if needed.
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

  uint32_t            vertexStride = 0;
  uint32_t            texcoordStride = 0;

  /*!
    \struct SoGeometryDesc::CacheHandle
    \brief Backend-owned cache objects associated with a geometry upload.

    Coin treats these pointers as opaque cache slots. The backend may populate
    them after uploading geometry so later frames can reuse the same GPU
    resources without reinterpreting the raw arrays.
  */
  struct CacheHandle {
    uint32_t contextId = 0;
    SoVBO * vertexVbo = nullptr;
    SoVBO * indexVbo = nullptr;
    SoVAO * vao = nullptr;
    const SoVertexLayout * vertexLayout = nullptr;
  } cache = {};
};

// --- Material flags (SoMaterialData::flags) ---
static constexpr uint32_t SO_MAT_HAS_TEXTURE = 0x1;  //!< Command carries embedded texture data
static constexpr uint32_t SO_MAT_IS_BILLBOARD = 0x2;  //!< Screen-space billboard sizing

// --- Feature flags (SoMaterialData::featureFlags) ---
static constexpr uint32_t SO_FEAT_BASE_COLOR = 0x1;   //!< Flat/unlit rendering (BASE_COLOR light model)

// --- Render param flags (SoRenderParams::flags) ---
static constexpr uint32_t SO_PARAM_CLEAR_WINDOW = 1u;
static constexpr uint32_t SO_PARAM_INTERACTIVE  = 2u;  //!< Camera orbiting/panning — skip ID buffer
static constexpr uint32_t SO_PARAM_CLEAR_DEPTH  = 4u;  //!< Clear depth buffer before rendering
static constexpr uint32_t SO_PARAM_SKIP_ID      = 8u;  //!< Skip ID buffer rendering entirely

/*!
  \struct SoTextureData
  \brief Embedded texture payload carried directly by a render command.

  This is used for commands that provide their own image data, such as SoImage.
  The memory is owned by the producer of the draw list and must remain valid
  until the backend finishes consuming the frame.
*/
struct SoTextureData {
  const unsigned char * pixels = nullptr;
  int width = 0;
  int height = 0;
  int numComponents = 0; // 1=L, 2=LA, 3=RGB, 4=RGBA
};

/*!
  \struct SoMaterialData
  \brief Snapshot of the logical Inventor material state for one draw call.

  Texture pointers are backend-defined handles; the IR does not own the memory.
*/
struct SoMaterialData {
  SbVec4f  diffuse = {0.8f, 0.8f, 0.8f, 1.0f};
  SbVec4f  ambient = {0.2f, 0.2f, 0.2f, 1.0f};
  SbVec4f  specular = {0.0f, 0.0f, 0.0f, 1.0f};
  SbVec4f  emissive = {0.0f, 0.0f, 0.0f, 1.0f};
  float    shininess = 0.2f;
  float    opacity = 1.0f;

  SoTextureData texture;  //!< Embedded texture (from SoImage, SoTexture2)

  void *   diffuseTexture = nullptr;
  void *   normalTexture = nullptr;
  void *   emissiveTexture = nullptr;

  float    metalness = 0.0f;
  float    roughness = 0.5f;

  uint32_t flags = 0;
  uint32_t featureFlags = 0;
};

/*!
  \struct SoDepthState
  \brief Depth-test configuration for a draw call.
*/
struct SoDepthState {
  SbBool  enabled = TRUE;
  SbBool  writeEnabled = TRUE;
  uint8_t func = 0; //!< Comparison function (GL-style enum value).
};

/*!
  \struct SoBlendState
  \brief Blending configuration (GL-style enums encoded as uint8_t).
*/
struct SoBlendState {
  SbBool  enabled = FALSE;
  uint8_t srcFactor = 0;
  uint8_t dstFactor = 0;
  uint8_t op = 0;
};

/*!
  \struct SoRasterState
  \brief Rasterizer properties (fill mode, culling, polygon offset).
*/
struct SoRasterState {
  uint8_t fillMode = 0;         // 0=filled, 1=lines (wireframe), 2=points
  uint8_t cullMode = 0;
  SbBool  scissorEnabled = FALSE;
  SbBool  clearDepth = FALSE;
  SbBool  viewportEnabled = FALSE;
  int     viewportX = 0;
  int     viewportY = 0;
  int     viewportWidth = 0;
  int     viewportHeight = 0;
  float   lineWidth = 1.0f;
  float   pointSize = 1.0f;
  uint16_t linePattern = 0xFFFF; // GL line stipple pattern (0xFFFF = solid)
  int16_t  linePatternScale = 1; // GL line stipple repeat factor
  float   polygonOffsetFactor = 0.0f;
  float   polygonOffsetUnits = 0.0f;
};

/*!
  \struct SoRenderState
  \brief Aggregates depth/blend/raster states plus precomputed sort keys.
*/
struct SoRenderState {
  SoDepthState depth;
  SoBlendState blend;
  SoRasterState raster;
  uint32_t opaqueKey = 0;
  uint32_t translucentKey = 0;
};

/*!
  \enum SoRenderPassType
  \brief Logical pass identifier used for coarse sorting.
*/
enum SoRenderPassType : uint8_t {
  SO_RENDERPASS_OPAQUE = 0,
  SO_RENDERPASS_TRANSPARENT,
  SO_RENDERPASS_AFTER_MAIN,
  SO_RENDERPASS_OVERLAY,
  SO_RENDERPASS_SHADOW,
  SO_RENDERPASS_CUSTOM,
  SO_RENDERPASS_COUNT
};

/*!
  \typedef SoLightingHandle
  \brief Stable 1-based handle into the draw list's deduplicated lighting table.
*/
typedef uint32_t SoLightingHandle;

/*!
  \typedef SoPipelineKey
  \brief Backend-defined key used to cache compiled pipeline state.
*/
typedef uint64_t SoPipelineKey;

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
  \brief View-space light description used by the render backend.
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
  \brief Shared lighting setup referenced by render commands.
*/
struct SoLightingData {
  SbVec3f ambient = SbVec3f(0.2f, 0.2f, 0.2f);
  std::vector<SoLightData> lights;
};

/*!
  \enum SoPickElementType
  \brief Element types for pick identification.
*/
enum SoPickElementType : uint8_t {
  SO_PICK_FACE = 0,
  SO_PICK_EDGE = 1,
  SO_PICK_VERTEX = 2,
  SO_PICK_WHOLE_BODY = 3
};

/*!
  \struct SoRenderElementRange
  \brief Maps one logical subelement to a draw subrange within a command.

  The range is expressed in the command's native draw domain:
  vertex offsets/counts for array draws, or index offsets/counts for indexed draws.
*/
struct SoRenderElementRange {
  SoPickElementType  elementType = SO_PICK_WHOLE_BODY;
  int                elementIndex = -1;  //!< Public 0-based element index
  int                drawStart = 0;      //!< Vertex or index start offset
  int                drawCount = 0;      //!< Number of vertices or indices
};

/*!
  \struct SoPickData
  \brief Per-command pick identification data for GPU ID buffer picking.

  Populated during SoIRRenderAction traversal. The pick LUT maps
  sequential IDs to (commandIndex, elementType, elementIndex) tuples.
  The pickIdentity string carries application-level naming context
  (e.g., "docName\tobjName\tsubPrefix" in FreeCAD).
*/
struct SoPickData {
  uint32_t    pickLutBase = 0;   //!< First LUT entry index for this command
  uint32_t    pickLutCount = 0;  //!< Number of LUT entries (faces + edges + vertices)
  std::string pickIdentity;      //!< Application pick identity (opaque to Coin)

  //! Logical subelement coverage for this command.
  //! Empty means the command is only pickable/selectable as a whole object.
  std::vector<SoRenderElementRange> elementRanges;
};

/*!
  \struct SoSelectionData
  \brief Mutable selection/highlight state for a render command.

  These fields can be modified directly (without re-traversal) to update
  preselection highlighting and click selection at interactive rates.
*/
struct SoSelectionData {
  bool        highlightWholeObject = false;  //!< Highlight the full command/object
  std::vector<int> highlightedElements;       //!< Public element IDs
  SbVec4f     highlightColor;
  bool        selectWholeObject = false;     //!< Select the full command/object
  std::vector<int> selectedElements;         //!< Public element IDs
  SbVec4f     selectionColor;

  //! Replace the highlighted element collection with one element.
  void setHighlightedElement(int element)
  {
    highlightedElements.clear();
    if (element >= 0) {
      highlightedElements.push_back(element);
    }
  }
};

/*!
  \struct SoRenderCommand
  \brief Complete description of a single draw call in the IR.
*/
struct SoRenderCommand {
  SoGeometryDesc   geometry;
  SoMaterialData   material;
  SoRenderState    state;

  SbMatrix         modelMatrix;  // default-constructed to identity
  SbMatrix         viewMatrix;
  SbMatrix         projMatrix;

  SoRenderPassType pass = SO_RENDERPASS_OPAQUE;
  SoLightingHandle lightingHandle = 0;
  SoPipelineKey    pipelineKey = 0;
  SoGLShaderProgram * shaderProgram = nullptr;

  SoPickData       pick;       //!< GPU pick identification
  SoSelectionData  selection;  //!< Mutable highlight/selection state

  uint64_t         sortKey = 0;
  void *           userData = nullptr;
};

/*!
  \struct SoPickLUTEntry
  \brief Maps a sequential render ID to a specific element of a draw command.
*/
struct SoPickLUTEntry {
  int                commandIndex;  //!< Index into SoDrawList
  SoPickElementType  elementType;
  int                elementIndex;  //!< Public face/edge/vertex index (0-based)

  // Draw parameters for the ID buffer pass
  int                drawStart;     //!< Vertex or index start offset
  int                drawCount;     //!< Vertex or index count
};

/*!
  \class SoDrawList
  \brief Container holding the ordered list of SoRenderCommand objects for a frame.
*/
class SoDrawList {
public:
  SoDrawList();

  void clear();
  void reserve(int count);

  uint32_t getGeneration() const { return generation; }

  void addCommand(const SoRenderCommand & cmd);
  SoRenderCommand & emplaceCommand();

  int getNumCommands() const;
  void truncate(int count);  //!< Remove commands beyond index count (for partial rebuild)
  SoRenderCommand & getCommand(int i);
  const SoRenderCommand & getCommand(int i) const;

  //! Add or reuse a lighting setup and return its stable 1-based handle.
  SoLightingHandle addLightingSetup(const SoLightingData & lighting);

  //! Resolve a lighting handle previously returned by addLightingSetup().
  //! Returns NULL for handle 0 or an invalid handle.
  const SoLightingData * getLighting(SoLightingHandle handle) const;

  SoRenderCommand * begin();
  SoRenderCommand * end();
  const SoRenderCommand * begin() const;
  const SoRenderCommand * end() const;

  // --- Pick LUT ---
  //! Access the pick look-up table (built after commands are collected).
  const std::vector<SoPickLUTEntry> & getPickLUT() const { return pickLUT; }
  std::vector<SoPickLUTEntry> & getMutablePickLUT() { return pickLUT; }

  //! Build a sorted index array for correct render ordering.
  //! The draw list itself is NOT reordered — command indices stay stable
  //! for pick LUT and command path lookups.
  void buildSortedOrder(const SbMatrix & viewMatrix);

  //! Get the sorted rendering order (indices into the command list).
  const std::vector<int> & getSortedOrder() const { return sortedOrder; }

  //! Generation counter — incremented on each buildPickLUT() call.
  //! Used by the backend to detect when ID color VBOs need rebuilding.
  uint64_t getPickLUTGeneration() const { return pickLUTGeneration; }

  //! Build the pick LUT from the current commands. Each face of BRep
  //! shapes gets a separate entry; edges/points/whole-body get one each.
  void buildPickLUT();

  //! Resolve a pick LUT index (1-based) to a pick identity string.
  //! Returns empty string if index is out of range.
  std::string resolvePickIdentity(uint32_t lutIndex) const;

private:
  std::vector<SoRenderCommand> commands;
  std::vector<SoLightingData> lightingSetups;
  std::vector<SoPickLUTEntry> pickLUT;
  std::vector<int> sortedOrder;
  uint32_t generation = 0;
  uint64_t pickLUTGeneration = 0;
};

#endif // COIN_SORENDERIR_H
