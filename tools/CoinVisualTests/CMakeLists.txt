find_package(glfw3 3.3 REQUIRED)
find_package(yaml-cpp REQUIRED)

set(COIN_VISUAL_TEST_DEFAULT_SPEC_DIR "${PROJECT_SOURCE_DIR}/testsuite/render/specs")
set(COIN_VISUAL_TEST_DEFAULT_ARTIFACT_DIR "${PROJECT_BINARY_DIR}/render_artifacts")
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoinVisualTestsConfig.h.in
  ${PROJECT_BINARY_DIR}/CoinVisualTestsConfig.h
  @ONLY)

add_executable(CoinVisualTests
  src/Main.cpp
  src/SnapshotCommand.cpp
  src/CompareCommand.cpp
  src/RunCommand.cpp
  src/GLFWBackend.cpp
  src/LegacyGLDriver.cpp
  src/RenderCore.cpp
  src/SpecLoader.cpp
  src/CompareCore.cpp)

target_include_directories(CoinVisualTests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/include/Inventor/annex
  ${PROJECT_BINARY_DIR}/include
  ${PROJECT_BINARY_DIR}
  ${COIN_TARGET_INCLUDE_DIRECTORIES})

target_compile_features(CoinVisualTests PRIVATE cxx_std_17)

target_link_libraries(CoinVisualTests PRIVATE
  glfw
  Coin
  stb
  yaml-cpp::yaml-cpp
  ${COIN_TARGET_LINK_LIBRARIES})
