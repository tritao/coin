#include "RenderWorkloadViewerController.h"

#include "GLRenderTestSession.h"
#include "RenderWorkloads.h"

#include <Inventor/SoRenderManager.h>
#include <Inventor/SbColor4f.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/rendering/SoRenderIR.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace coin_test {

struct RenderWorkloadViewerControllerImpl {
  GLRenderTestSession & session;
  GLTestContext & context;
  SoRenderManager & manager;
  SoOrthographicCamera & camera;
  SceneMutationHandles & mutations;
  int width;
  int height;
  double cursorX = 0.0;
  double cursorY = 0.0;
  bool panning = false;
  bool animate = false;
  bool paused = false;
  bool hasHover = false;
  bool hasSelection = false;
  bool pickPending = false;
  SoPickIdentity hover;
  SoSelectionTarget selected;
  SoAsyncPickRequest pickRequest;
  std::vector<SbVec3f> baseTranslations;
  std::chrono::steady_clock::time_point animationStart;

  RenderWorkloadViewerControllerImpl(
       GLRenderTestSession & sessionValue,
       SoOrthographicCamera & cameraValue,
       SceneMutationHandles & mutationValue, int widthValue, int heightValue)
    : session(sessionValue), context(sessionValue.context()),
      manager(sessionValue.manager()), camera(cameraValue),
      mutations(mutationValue), width(widthValue), height(heightValue),
      animationStart(std::chrono::steady_clock::now())
  {
    baseTranslations.reserve(mutations.transforms.size());
    for (SoTranslation * transform : mutations.transforms)
      baseTranslations.push_back(transform->translation.getValue());
  }
};

namespace {

RenderWorkloadViewerControllerImpl * state(GLFWwindow * window)
{
  return static_cast<RenderWorkloadViewerControllerImpl *>(
    glfwGetWindowUserPointer(window));
}

SoSelectionTarget selectionTarget(const SoPickIdentity & identity,
                                  const SbColor4f & color)
{
  SoSelectionTarget target;
  target.commandIndex = identity.commandIndex;
  target.nodeId = identity.nodeId;
  target.instanceId = identity.instanceId;
  target.objectId = identity.objectId;
  target.type = identity.type;
  target.elementIndex = identity.elementIndex;
  target.color = color;
  return target;
}

void updateSelection(RenderWorkloadViewerControllerImpl & viewer)
{
  SoSelectionState selection;
  if (viewer.hasSelection) selection.selected.push_back(viewer.selected);
  if (viewer.hasHover) {
    selection.highlighted.push_back(selectionTarget(
      viewer.hover, SbColor4f(0.2f, 0.8f, 1.0f, 0.75f)));
  }
  viewer.manager.setSelectionState(selection);
}

void framebufferSizeCallback(GLFWwindow * window, int width, int height)
{
  RenderWorkloadViewerControllerImpl * viewer = state(window);
  if (!viewer || width <= 0 || height <= 0) return;
  if (!viewer->session.resize(width, height)) return;
  viewer->width = width;
  viewer->height = height;
}

void scrollCallback(GLFWwindow * window, double, double yoffset)
{
  RenderWorkloadViewerControllerImpl * viewer = state(window);
  if (!viewer) return;
  const float factor = std::pow(0.85f, static_cast<float>(yoffset));
  viewer->camera.height = std::max(
    0.01f, viewer->camera.height.getValue() * factor);
}

void mouseButtonCallback(GLFWwindow * window, int button, int action, int)
{
  RenderWorkloadViewerControllerImpl * viewer = state(window);
  if (!viewer) return;
  if (button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_MIDDLE)
    viewer->panning = action == GLFW_PRESS;
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS &&
      viewer->hasHover) {
    viewer->selected = selectionTarget(
      viewer->hover, SbColor4f(1.0f, 0.8f, 0.0f, 0.65f));
    viewer->hasSelection = true;
    updateSelection(*viewer);
  }
}

void cursorCallback(GLFWwindow * window, double x, double y)
{
  RenderWorkloadViewerControllerImpl * viewer = state(window);
  if (!viewer) return;
  if (viewer->panning) {
    const float scale = viewer->camera.height.getValue() /
      static_cast<float>(std::max(1, viewer->height));
    SbVec3f position = viewer->camera.position.getValue();
    position[0] -= static_cast<float>(x - viewer->cursorX) * scale;
    position[1] += static_cast<float>(y - viewer->cursorY) * scale;
    viewer->camera.position = position;
  }
  viewer->cursorX = x;
  viewer->cursorY = y;
}

void keyCallback(GLFWwindow * window, int key, int, int action, int)
{
  if (action != GLFW_PRESS) return;
  RenderWorkloadViewerControllerImpl * viewer = state(window);
  if (!viewer) return;
  if (key == GLFW_KEY_M) viewer->animate = !viewer->animate;
  else if (key == GLFW_KEY_SPACE) viewer->paused = !viewer->paused;
  else if (key == GLFW_KEY_R) viewer->manager.invalidateDrawList();
  else if (key == GLFW_KEY_C) {
    viewer->hasSelection = false;
    updateSelection(*viewer);
  }
}

} // namespace

RenderWorkloadViewerController::RenderWorkloadViewerController(
  GLRenderTestSession & session,
  SoOrthographicCamera & camera, SceneMutationHandles & mutations,
  int width, int height)
  : impl_(new RenderWorkloadViewerControllerImpl(
      session, camera, mutations, width, height))
{
}

RenderWorkloadViewerController::~RenderWorkloadViewerController()
{
  if (impl_->context.window())
    glfwSetWindowUserPointer(impl_->context.window(), nullptr);
  delete impl_;
}

void RenderWorkloadViewerController::attach()
{
  GLFWwindow * window = impl_->context.window();
  glfwSetWindowUserPointer(window, impl_);
  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
  glfwSetScrollCallback(window, scrollCallback);
  glfwSetMouseButtonCallback(window, mouseButtonCallback);
  glfwSetCursorPosCallback(window, cursorCallback);
  glfwSetKeyCallback(window, keyCallback);
}

bool RenderWorkloadViewerController::pollEvents()
{
  impl_->context.pollEvents();
  if (impl_->paused) glfwWaitEventsTimeout(0.05);
  return !impl_->paused;
}

void RenderWorkloadViewerController::beforeRender()
{
  if (!impl_->animate || impl_->mutations.transforms.empty()) return;
  const double seconds = std::chrono::duration<double>(
    std::chrono::steady_clock::now() - impl_->animationStart).count();
  SbVec3f position = impl_->baseTranslations.front();
  position[0] += 0.25f * std::sin(static_cast<float>(seconds * 5.0));
  impl_->mutations.transforms.front()->translation = position;
}

void RenderWorkloadViewerController::afterRender(const bool pickingEnabled)
{
  if (!pickingEnabled) return;
  if (impl_->pickPending) {
    SoPickIdentity identity;
    const SoAsyncPickStatus status =
      impl_->manager.pollPickIdentityAsync(impl_->pickRequest, identity);
    if (status != SoAsyncPickStatus::PENDING) {
      impl_->pickPending = false;
      const bool hit = status == SoAsyncPickStatus::HIT;
      if (hit != impl_->hasHover ||
          (hit && (identity.commandIndex != impl_->hover.commandIndex ||
                   identity.elementIndex != impl_->hover.elementIndex))) {
        impl_->hasHover = hit;
        if (hit) impl_->hover = identity;
        updateSelection(*impl_);
      }
    }
  }
  if (!impl_->pickPending) {
    const int pickX = static_cast<int>(impl_->cursorX);
    const int pickY = impl_->height - 1 - static_cast<int>(impl_->cursorY);
    impl_->pickPending = impl_->manager.requestPickIdentityAsync(
      pickX, pickY, 2, impl_->pickRequest) != FALSE;
  }
}

bool RenderWorkloadViewerController::resize(const int width, const int height)
{
  if (!impl_->session.resize(width, height)) return false;
  impl_->width = width;
  impl_->height = height;
  return true;
}

void RenderWorkloadViewerController::setCursorPosition(double x, double y)
{
  impl_->cursorX = x;
  impl_->cursorY = y;
}

void RenderWorkloadViewerController::setAnimationEnabled(const bool enabled)
{
  impl_->animate = enabled;
}

bool RenderWorkloadViewerController::hasHoverTarget() const
{
  return impl_->hasHover;
}

} // namespace coin_test
