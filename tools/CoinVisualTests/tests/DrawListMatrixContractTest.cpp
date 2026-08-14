#include "DrawListFrameContract.h"

#include <iostream>

namespace {

SoRenderCommand commandWithMatrices(const SbMatrix& view,
                                    const SbMatrix& projection) {
  SoRenderCommand command;
  command.viewMatrix = view;
  command.projMatrix = projection;
  return command;
}

bool checkRejects(const SbMatrix& first_view,
                  const SbMatrix& first_projection,
                  const SbMatrix& second_view,
                  const SbMatrix& second_projection) {
  SoDrawList drawlist;
  drawlist.addCommand(commandWithMatrices(first_view, first_projection));
  drawlist.addCommand(commandWithMatrices(second_view, second_projection));

  SbMatrix view;
  SbMatrix projection;
  std::string error;
  if (CoinVisualTests::extractFrameMatrices(drawlist, view, projection, error)) {
    return false;
  }
  return !error.empty();
}

} // namespace

int main() {
  SbMatrix identity;
  identity.makeIdentity();

  SbMatrix translated;
  translated.makeIdentity();
  translated[3][0] = 1.0f;

  if (!checkRejects(identity, identity, translated, identity)) {
    std::cerr << "FAIL: differing view matrices were accepted\n";
    return 1;
  }
  if (!checkRejects(identity, identity, identity, translated)) {
    std::cerr << "FAIL: differing projection matrices were accepted\n";
    return 1;
  }
  return 0;
}
