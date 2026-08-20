#ifndef COIN_TEST_RENDERWORKLOADVIEWERCONTROLLER_H
#define COIN_TEST_RENDERWORKLOADVIEWERCONTROLLER_H

class GLRenderTestSession;
class SoOrthographicCamera;
class SoRenderManager;

namespace coin_test {
struct SceneMutationHandles;
struct RenderWorkloadViewerControllerImpl;

class RenderWorkloadViewerController {
public:
  RenderWorkloadViewerController(GLRenderTestSession & session,
                                 SoOrthographicCamera & camera,
                                 SceneMutationHandles & mutations,
                                 int width, int height);
  ~RenderWorkloadViewerController();

  void attach();
  bool pollEvents();
  void beforeRender();
  void afterRender(bool pickingEnabled);
  bool resize(int width, int height);
  void setCursorPosition(double x, double y);
  void setAnimationEnabled(bool enabled);
  bool hasHoverTarget() const;

private:
  RenderWorkloadViewerControllerImpl * impl_;
};

} // namespace coin_test

#endif // COIN_TEST_RENDERWORKLOADVIEWERCONTROLLER_H
