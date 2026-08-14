#include "DrawListFrameContract.h"

namespace CoinVisualTests {

bool extractFrameMatrices(const SoDrawList& drawlist,
                          SbMatrix& view,
                          SbMatrix& projection,
                          std::string& error) {
  view.makeIdentity();
  projection.makeIdentity();
  error.clear();

  if (drawlist.getNumCommands() == 0) {
    return true;
  }

  const SoRenderCommand& first = drawlist.getCommand(0);
  view = first.viewMatrix;
  projection = first.projMatrix;
  for (int i = 1; i < drawlist.getNumCommands(); ++i) {
    const SoRenderCommand& command = drawlist.getCommand(i);
    if (!view.equals(command.viewMatrix, 1.0e-6f) ||
        !projection.equals(command.projMatrix, 1.0e-6f)) {
      error = "DrawList visual execution requires one view/projection pair "
              "for every command";
      return false;
    }
  }
  return true;
}

} // namespace CoinVisualTests
