#ifndef COIN_VISUAL_TESTS_DRAW_LIST_FRAME_CONTRACT_H
#define COIN_VISUAL_TESTS_DRAW_LIST_FRAME_CONTRACT_H

#include <string>

#include <Inventor/SbVec2f.h>
#include <Inventor/rendering/SoRenderIR.h>

namespace CoinVisualTests {

// The retained executor accepts one frame-level view/projection pair. Reject a
// retained frame that would otherwise render command-local camera state using
// the wrong matrices.
bool extractFrameMatrices(const SoDrawList& drawlist,
                          SbMatrix& view,
                          SbMatrix& projection,
                          std::string& error);

} // namespace CoinVisualTests

#endif
