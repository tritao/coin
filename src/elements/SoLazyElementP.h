#ifndef COIN_SOLAZYELEMENTP_H
#define COIN_SOLAZYELEMENTP_H

#include <Inventor/SbBasic.h>
#include <Inventor/lists/SbList.h>

class SoLazyElement;
class SoNode;
class SoState;

// Source-private access to metadata describing SoLazyElement's packed color
// representation. The state remains owned by SoLazyElement's pimpl; these
// helpers are not part of Coin's public element API.
class SoLazyElementP {
public:
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
