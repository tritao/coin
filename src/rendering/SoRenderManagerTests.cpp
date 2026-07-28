/**************************************************************************\
 * Copyright (c) 2026 The Coin3D contributors                          *
 *                                                                        *
 * This file is part of Coin.                                            *
 *                                                                        *
 * Coin is free software; you can redistribute it and/or modify it under *
 * the terms of the GNU General Public License as published by the Free  *
 * Software Foundation; either version 2 of the License, or (at your      *
 * option) any later version.                                            *
\**************************************************************************/

#include <Inventor/SoRenderManager.h>

#ifdef COIN_TEST_SUITE

#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/elements/SoDevicePixelRatioElement.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/rendering/SoRenderIR.h>

#include <cstdio>
#include <cstdlib>

class TestDevicePixelRatioElement : public SoDevicePixelRatioElement
{
public:
  float value() const { return this->data; }
  void setValue(float value) { this->setElt(value); }
};

class CountingGLRenderAction : public SoGLRenderAction
{
public:
  explicit CountingGLRenderAction(const SbViewportRegion & viewport)
    : SoGLRenderAction(viewport)
  {
  }

  void invalidateState() override
  {
    ++this->invalidationCount;
    SoGLRenderAction::invalidateState();
  }

  int invalidationCount = 0;
};

struct StageProbe {
  int calls = 0;
};

static void appendIdentifiableIRCommand(void * userdata,
                                        SoRenderManager *,
                                        SoAction * action)
{
  if (!action->isOfType(SoIRRenderAction::getClassTypeId())) {
    return;
  }

  SoIRRenderAction * irAction = static_cast<SoIRRenderAction *>(action);
  SoRenderCommand & command = irAction->getMutableDrawList().emplaceCommand();
  command.userData = userdata;
  ++static_cast<StageProbe *>(userdata)->calls;
}

struct ManagerRenderProbe {
  SoRenderManager * manager = nullptr;
  int calls = 0;
};

struct SharedContextRenderProbe {
  SoRenderManager * manager = nullptr;
  CountingGLRenderAction * sharedAction = nullptr;
  SoNode * probeScene = nullptr;
  uint32_t context = 0;
  int invalidationsBeforeBackend = 0;
  int invalidationsAfterBackend = 0;
};

static void renderManagerFromCallback(void * userdata, SoAction * action)
{
  if (!action->isOfType(SoGLRenderAction::getClassTypeId())) {
    return;
  }

  ManagerRenderProbe * probe = static_cast<ManagerRenderProbe *>(userdata);
  ++probe->calls;
  probe->manager->render();
}

static void renderManagerAndSharedAction(void * userdata, SoAction * action)
{
  if (!action->isOfType(SoGLRenderAction::getClassTypeId())) {
    return;
  }

  auto * probe = static_cast<SharedContextRenderProbe *>(userdata);
  auto * outerAction = static_cast<SoGLRenderAction *>(action);
  const uint32_t context = SoGLCacheContextElement::get(outerAction->getState());
  if (probe->context == 0) {
    probe->context = context;
    probe->manager->getGLRenderAction()->setCacheContext(context);
    probe->sharedAction->setCacheContext(context);
  }

  probe->sharedAction->apply(probe->probeScene);
  probe->invalidationsBeforeBackend = probe->sharedAction->invalidationCount;

  probe->manager->render();

  probe->sharedAction->apply(probe->probeScene);
  probe->invalidationsAfterBackend = probe->sharedAction->invalidationCount;
}

static bool renderTestsHaveDisplay(void)
{
  if (std::getenv("DISPLAY")) {
    return true;
  }

  std::fprintf(stderr,
               "[SKIP] SoRenderManager offscreen tests require DISPLAY\n");
  return false;
}

static SbBool renderWithManager(SoRenderManager & manager,
                                SoSeparator * outer)
{
  const SbViewportRegion viewport(32, 32);
  manager.setViewportRegion(viewport);
  SoGLRenderAction outerAction(viewport);
  SoOffscreenRenderer renderer(&outerAction);
  renderer.setViewportRegion(viewport);
  return renderer.render(outer);
}

BOOST_AUTO_TEST_CASE(device_pixel_ratio_defaults_to_one)
{
  TestDevicePixelRatioElement element;
  element.init(nullptr);

  BOOST_CHECK(SoDevicePixelRatioElement::getClassStackIndex() >= 0);
  BOOST_CHECK_EQUAL(element.value(), 1.0f);
  element.setValue(2.0f);
  BOOST_CHECK_EQUAL(element.value(), 2.0f);
}

BOOST_AUTO_TEST_CASE(render_manager_defaults_and_policy_state)
{
  SoRenderManager manager;

  BOOST_CHECK(manager.getRenderPipeline() ==
              SoRenderManager::RenderPipeline::LEGACY_GL);
  BOOST_CHECK_EQUAL(manager.getDevicePixelRatio(), 1.0f);
  BOOST_CHECK_EQUAL(manager.getRenderMode(), SoRenderManager::AS_IS);
  BOOST_CHECK_EQUAL(manager.getLightingMode(), SoRenderManager::LIT);

  manager.setDevicePixelRatio(2.0f);
  manager.setLightingMode(SoRenderManager::UNLIT);
  manager.setRenderMode(SoRenderManager::WIREFRAME);

  BOOST_CHECK_EQUAL(manager.getDevicePixelRatio(), 2.0f);
  BOOST_CHECK_EQUAL(manager.getLightingMode(), SoRenderManager::UNLIT);
  BOOST_CHECK_EQUAL(manager.getRenderMode(), SoRenderManager::WIREFRAME);
}

BOOST_AUTO_TEST_CASE(render_manager_pipeline_switching)
{
  SoRenderManager manager;
  CountingGLRenderAction action(SbViewportRegion(1, 1));
  manager.setGLRenderAction(&action);
  const uint32_t context = SoGLCacheContextElement::getUniqueCacheContext();
  action.setCacheContext(context);
  const uint64_t initialgeneration =
    SoGLCacheContextElement::getContextStateGeneration(context);
  action.invalidationCount = 0;

  manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
  BOOST_CHECK(manager.getRenderPipeline() ==
              SoRenderManager::RenderPipeline::DRAW_LIST);

  manager.setRenderPipeline(SoRenderManager::RenderPipeline::LEGACY_GL);
  BOOST_CHECK(manager.getRenderPipeline() ==
              SoRenderManager::RenderPipeline::LEGACY_GL);
  BOOST_CHECK_EQUAL(action.invalidationCount, 2);
  BOOST_CHECK_EQUAL(
    SoGLCacheContextElement::getContextStateGeneration(context), initialgeneration + 2);
}

BOOST_AUTO_TEST_CASE(shared_context_state_generation)
{
  const uint32_t context = SoGLCacheContextElement::getUniqueCacheContext();
  const uint64_t before = SoGLCacheContextElement::getContextStateGeneration(context);

  SoGLCacheContextElement::invalidateContextState(context);

  BOOST_CHECK_EQUAL(
    SoGLCacheContextElement::getContextStateGeneration(context), before + 1);
}

BOOST_AUTO_TEST_CASE(draw_list_invalidates_all_shared_gl_actions)
{
  if (!renderTestsHaveDisplay()) {
    return;
  }

  SoRenderManager manager;
  CountingGLRenderAction managerAction(SbViewportRegion(32, 32));
  CountingGLRenderAction sharedAction(SbViewportRegion(32, 32));
  manager.setGLRenderAction(&managerAction);
  manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);

  SoSeparator * scene = new SoSeparator;
  scene->ref();

  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0.0f, 0.0f, 0.0f);
  camera->nearDistance.setValue(0.1f);
  camera->farDistance.setValue(10.0f);
  camera->height.setValue(2.0f);
  scene->addChild(camera);
  manager.setCamera(camera);
  camera->unref();

  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(0.8f, 0.2f, 0.1f);
  scene->addChild(material);

  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.set1Value(0, -0.8f, -0.8f, -1.0f);
  coordinates->point.set1Value(1, 0.8f, -0.8f, -1.0f);
  coordinates->point.set1Value(2, 0.8f, 0.8f, -1.0f);
  coordinates->point.set1Value(3, -0.8f, 0.8f, -1.0f);
  scene->addChild(coordinates);

  SoFaceSet * faces = new SoFaceSet;
  faces->numVertices.setValue(4);
  scene->addChild(faces);

  manager.setSceneGraph(scene);
  scene->unref();

  SoSeparator * outer = new SoSeparator;
  outer->ref();

  SharedContextRenderProbe probe;
  probe.manager = &manager;
  probe.sharedAction = &sharedAction;
  probe.probeScene = scene;

  SoCallback * callback = new SoCallback;
  callback->setCallback(renderManagerAndSharedAction, &probe);
  outer->addChild(callback);

  BOOST_REQUIRE(renderWithManager(manager, outer));
  BOOST_CHECK(probe.context != 0);
  BOOST_CHECK(probe.invalidationsAfterBackend > probe.invalidationsBeforeBackend);

  outer->unref();
}

BOOST_AUTO_TEST_CASE(scene_invalidation_does_not_invalidate_legacy_action_state)
{
  SoRenderManager manager;
  CountingGLRenderAction action(SbViewportRegion(1, 1));
  manager.setGLRenderAction(&action);
  action.invalidationCount = 0;

  manager.invalidateScene();

  BOOST_CHECK_EQUAL(action.invalidationCount, 0);
}

BOOST_AUTO_TEST_CASE(after_main_commands_survive_foreground_rebuild)
{
  SoIRRenderAction action(SbViewportRegion(1, 1));
  StageProbe probe;

  action.getMutableDrawList().emplaceCommand();
  appendIdentifiableIRCommand(&probe, nullptr, &action);
  const int afterMainCheckpoint = action.getDrawList().getNumCommands();

  action.getMutableDrawList().emplaceCommand();
  action.getMutableDrawList().truncate(afterMainCheckpoint);

  BOOST_CHECK_EQUAL(action.getDrawList().getNumCommands(), 2);
  BOOST_CHECK(action.getDrawList().getCommand(1).userData == &probe);
  BOOST_CHECK_EQUAL(probe.calls, 1);
}

BOOST_AUTO_TEST_CASE(after_main_stage_has_depth_barrier_and_order)
{
  SoIRRenderAction action(SbViewportRegion(1, 1));
  int mainTag = 1;
  int afterMainTag = 2;
  int secondAfterMainTag = 3;
  int foregroundTag = 4;

  SoRenderCommand & mainCommand = action.getMutableDrawList().emplaceCommand();
  mainCommand.userData = &mainTag;

  action.beginAfterMainStage();
  SoRenderCommand & afterMainCommand = action.getMutableDrawList().emplaceCommand();
  afterMainCommand.userData = &afterMainTag;
  action.applyRenderStage(afterMainCommand);

  SoRenderCommand & secondAfterMainCommand = action.getMutableDrawList().emplaceCommand();
  secondAfterMainCommand.userData = &secondAfterMainTag;
  action.applyRenderStage(secondAfterMainCommand);
  action.endAfterMainStage();

  SoRenderCommand & foregroundCommand = action.getMutableDrawList().emplaceCommand();
  foregroundCommand.userData = &foregroundTag;

  BOOST_CHECK_EQUAL(action.getDrawList().getNumCommands(), 4);
  BOOST_CHECK(action.getDrawList().getCommand(0).userData == &mainTag);
  BOOST_CHECK(action.getDrawList().getCommand(1).userData == &afterMainTag);
  BOOST_CHECK(action.getDrawList().getCommand(2).userData == &secondAfterMainTag);
  BOOST_CHECK(action.getDrawList().getCommand(3).userData == &foregroundTag);
  BOOST_CHECK_EQUAL(action.getDrawList().getCommand(1).pass, SO_RENDERPASS_AFTER_MAIN);
  BOOST_CHECK(action.getDrawList().getCommand(1).state.raster.clearDepth);
  BOOST_CHECK_EQUAL(action.getDrawList().getCommand(2).pass, SO_RENDERPASS_AFTER_MAIN);
  BOOST_CHECK(!action.getDrawList().getCommand(2).state.raster.clearDepth);
  BOOST_CHECK_EQUAL(action.getDrawList().getCommand(3).pass, SO_RENDERPASS_OPAQUE);
}

BOOST_AUTO_TEST_CASE(manager_after_main_callback_survives_foreground_rebuild)
{
  if (!renderTestsHaveDisplay()) {
    return;
  }

  SoRenderManager manager;
  manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);

  SoSeparator * scene = new SoSeparator;
  scene->ref();
  manager.setSceneGraph(scene);
  scene->unref();

  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0.0f, 0.0f, 0.0f);
  camera->ref();
  manager.setCamera(camera);
  camera->unref();

  SoSeparator * outer = new SoSeparator;
  outer->ref();
  outer->addChild(manager.getCamera());

  ManagerRenderProbe renderProbe;
  renderProbe.manager = &manager;
  SoCallback * renderCallback = new SoCallback;
  renderCallback->setCallback(renderManagerFromCallback, &renderProbe);
  outer->addChild(renderCallback);

  StageProbe stageProbe;
  manager.addAfterMainSceneCallback(appendIdentifiableIRCommand, &stageProbe);

  SbBool rendered = renderWithManager(manager, outer);
  BOOST_REQUIRE(rendered);
  BOOST_REQUIRE(manager.getIRRenderAction());
  const int firstCommandCount = manager.getIRRenderAction()->getDrawList().getNumCommands();

  manager.invalidateForeground();
  rendered = renderWithManager(manager, outer);
  BOOST_REQUIRE(rendered);

  BOOST_CHECK_EQUAL(renderProbe.calls, 2);
  BOOST_CHECK_EQUAL(stageProbe.calls, 1);
  BOOST_CHECK_EQUAL(manager.getIRRenderAction()->getDrawList().getNumCommands(),
                    firstCommandCount);

  outer->unref();
}

BOOST_AUTO_TEST_CASE(hidden_line_preserves_explicit_background_layer)
{
  if (!renderTestsHaveDisplay()) {
    return;
  }

  SoRenderManager manager;
  manager.setRenderMode(SoRenderManager::HIDDEN_LINE);
  manager.setLightingMode(SoRenderManager::UNLIT);

  SoSeparator * scene = new SoSeparator;
  scene->ref();
  manager.setSceneGraph(scene);
  scene->unref();

  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0.0f, 0.0f, 0.0f);
  camera->ref();
  manager.setCamera(camera);
  camera->unref();

  SoSeparator * background = new SoSeparator;
  SoSeparator * left = new SoSeparator;
  SoMaterial * leftMaterial = new SoMaterial;
  leftMaterial->diffuseColor.setValue(1.0f, 0.0f, 0.0f);
  SoCoordinate3 * leftCoordinates = new SoCoordinate3;
  leftCoordinates->point.set1Value(0, -1.0f, -1.0f, -1.0f);
  leftCoordinates->point.set1Value(1, 0.0f, -1.0f, -1.0f);
  leftCoordinates->point.set1Value(2, 0.0f, 1.0f, -1.0f);
  leftCoordinates->point.set1Value(3, -1.0f, 1.0f, -1.0f);
  SoFaceSet * leftFaces = new SoFaceSet;
  leftFaces->numVertices.setValue(4);
  left->addChild(leftMaterial);
  left->addChild(leftCoordinates);
  left->addChild(leftFaces);

  SoSeparator * right = new SoSeparator;
  SoMaterial * rightMaterial = new SoMaterial;
  rightMaterial->diffuseColor.setValue(0.0f, 1.0f, 0.0f);
  SoCoordinate3 * rightCoordinates = new SoCoordinate3;
  rightCoordinates->point.set1Value(0, 0.0f, -1.0f, -1.0f);
  rightCoordinates->point.set1Value(1, 1.0f, -1.0f, -1.0f);
  rightCoordinates->point.set1Value(2, 1.0f, 1.0f, -1.0f);
  rightCoordinates->point.set1Value(3, 0.0f, 1.0f, -1.0f);
  SoFaceSet * rightFaces = new SoFaceSet;
  rightFaces->numVertices.setValue(4);
  right->addChild(rightMaterial);
  right->addChild(rightCoordinates);
  right->addChild(rightFaces);

  background->addChild(left);
  background->addChild(right);
  background->ref();
  manager.setRenderLayerRoot(SoRenderManager::RENDER_LAYER_BACKGROUND, background);
  background->unref();

  SoSeparator * outer = new SoSeparator;
  outer->ref();
  outer->addChild(manager.getCamera());
  ManagerRenderProbe renderProbe;
  renderProbe.manager = &manager;
  SoCallback * renderCallback = new SoCallback;
  renderCallback->setCallback(renderManagerFromCallback, &renderProbe);
  outer->addChild(renderCallback);

  const SbViewportRegion viewport(32, 32);
  manager.setViewportRegion(viewport);
  SoGLRenderAction outerAction(viewport);
  SoOffscreenRenderer renderer(&outerAction);
  renderer.setViewportRegion(viewport);
  SbBool rendered = renderer.render(outer);
  BOOST_REQUIRE(rendered);
  const unsigned char * pixels = renderer.getBuffer();
  BOOST_CHECK(pixels);

  const unsigned char * leftPixel = pixels + ((16 * 32) + 8) * 3;
  const unsigned char * rightPixel = pixels + ((16 * 32) + 24) * 3;
  BOOST_CHECK(leftPixel[0] > leftPixel[1]);
  BOOST_CHECK(rightPixel[1] > rightPixel[0]);

  outer->unref();
}

BOOST_AUTO_TEST_CASE(highlight_storage_supports_multiple_elements)
{
  SoSelectionData selection;
  selection.setHighlightedElement(4);
  BOOST_CHECK_EQUAL(selection.highlightedElements.size(), size_t(1));
  BOOST_CHECK_EQUAL(selection.highlightedElements[0], 4);

  selection.highlightedElements.push_back(17);
  selection.highlightedElements.push_back(29);
  BOOST_CHECK_EQUAL(selection.highlightedElements.size(), size_t(3));
  BOOST_CHECK_EQUAL(selection.highlightedElements[1], 17);
  BOOST_CHECK_EQUAL(selection.highlightedElements[2], 29);

  selection.setHighlightedElement(-1);
  BOOST_CHECK(selection.highlightedElements.empty());
}

#endif // COIN_TEST_SUITE
