#ifndef COIN_TEST_GLRENDERTESTSESSION_H
#define COIN_TEST_GLRENDERTESTSESSION_H

#include "GLTestContext.h"

#include <Inventor/SoRenderManager.h>

#include <cstdint>
#include <vector>

class SoCamera;
class SoNode;

struct GLRenderTestConfig {
  GLTestProfile profile = GLTestProfile::Core;
  SoRenderManager::RenderPipeline pipeline =
    SoRenderManager::RenderPipeline::DRAW_LIST;
  int width = 64;
  int height = 64;
  bool visible = false;
  bool vsync = false;
};

class GLRenderTestSession {
public:
  GLRenderTestSession();
  ~GLRenderTestSession();

  GLRenderTestSession(const GLRenderTestSession &) = delete;
  GLRenderTestSession & operator=(const GLRenderTestSession &) = delete;

  bool initialize(const GLRenderTestConfig & config);
  void shutdown();
  void setScene(SoNode * scene, SoCamera * camera);
  bool render(bool clearColor = true, bool clearDepth = true);
  bool resize(int width, int height);
  std::vector<uint8_t> readPixels() const;

  bool initialized() const;
  GLTestContext & context();
  SoRenderManager & manager();
  const SoRenderManager & manager() const;

private:
  struct Impl;
  Impl * impl_;
};

#endif // COIN_TEST_GLRENDERTESTSESSION_H
