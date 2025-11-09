#ifndef COIN_VISUAL_TESTS_SPEC_DIR_RESOLVER_H
#define COIN_VISUAL_TESTS_SPEC_DIR_RESOLVER_H

#include <string>

#include "CoinVisualTestsConfig.h"

namespace CoinVisualTests {

namespace SpecDir {

inline std::string computeDefaultSpecDir() {
  return COIN_VISUAL_TEST_DEFAULT_SPEC_DIR;
}

inline std::string computeArtifactsBase() {
  return COIN_VISUAL_TEST_DEFAULT_ARTIFACT_DIR;
}

} // namespace SpecDir

} // namespace CoinVisualTests

#endif
