#ifndef COIN_SOLAZYELEMENTP_H
#define COIN_SOLAZYELEMENTP_H

#include <Inventor/SbBasic.h>
#include <Inventor/SbColor.h>
#include <Inventor/lists/SbList.h>

class SoLazyElement;
class SoNode;
class SoState;

// Source-private access to metadata describing SoLazyElement's packed color
// representation. The state remains owned by SoLazyElement's pimpl; these
// helpers are not part of Coin's public element API.
class SoLazyElementP {
public:
  struct RenderSnapshot {
    SbColor diffuse;
    SbColor ambient;
    SbColor emissive;
    SbColor specular;
    float transparency = 0.0f;
    float shininess = 0.0f;
    float packedOpacity = 1.0f;
    float alphaTestValue = 0.5f;
    int lightModel = 0;
    int blendSource = 0;
    int blendDestination = 0;
    int alphaBlendSource = 0;
    int alphaBlendDestination = 0;
    int alphaTestFunction = 0;
    SbBool blending = FALSE;
    SbBool separateBlending = FALSE;
    SbBool twoSidedLighting = FALSE;
    SbBool packedVertexColors = FALSE;
  };

  SoLazyElementP() = default;

  static void setPackedVertexColors(
    SoState * state, SoNode * node, int32_t numcolors,
    const uint32_t * colors, SbBool packedtransparency);
  static SbBool hasPackedVertexColorState(SoState * state);
  static float getPackedVertexColorOpacity(SoState * state,
                                           int materialIndex);
  static void setAlphaTestSemantic(SoState * state, int function,
                                   float value);
  static int getAlphaTestSemantic(SoState * state, float & value);
  static RenderSnapshot captureRenderSnapshot(SoState * state,
                                              int materialIndex);

private:
  friend class SoLazyElement;

  static void capturePackedVertexColorOpacities(
    SoState * state, SbList<float> & opacities);
  static void setPackedVertexColorState(
    SoState * state, const SbList<float> & opacities);
  static void clearPackedVertexColorState(SoState * state);

  struct PackedColorState {
    SbBool fromVertexProperty = FALSE;
    SbList<float> inheritedOpacities;
  } packedColor;

  struct SemanticAlphaTestState {
    int function = 0;
    float value = 0.5f;
  } semanticAlphaTest;
};

#endif // COIN_SOLAZYELEMENTP_H
