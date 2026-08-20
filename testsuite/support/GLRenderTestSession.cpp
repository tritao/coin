#include "GLRenderTestSession.h"

#include <Inventor/SbViewportRegion.h>
#include <Inventor/nodes/SoCamera.h>
#if COIN_HAVE_LEGACY_GL_RENDERER
#include <Inventor/actions/SoGLRenderAction.h>
#endif

#include <iostream>

struct GLRenderTestSession::Impl {
  GLTestContext context;
  SoRenderManager manager;
#if COIN_HAVE_LEGACY_GL_RENDERER
  SoGLRenderAction * legacyAction = nullptr;
#endif
  bool initialized = false;
  int width = 0;
  int height = 0;
};

GLRenderTestSession::GLRenderTestSession()
  : impl_(new Impl)
{
}

GLRenderTestSession::~GLRenderTestSession()
{
  this->shutdown();
  delete impl_;
}

bool GLRenderTestSession::initialize(const GLRenderTestConfig & config)
{
  if (impl_->initialized) return true;
  if (config.pipeline == SoRenderManager::RenderPipeline::LEGACY_GL &&
      config.profile != GLTestProfile::Compatibility) {
    std::cerr << "LegacyGL requires a compatibility OpenGL profile"
              << std::endl;
    return false;
  }
#if !COIN_HAVE_LEGACY_GL_RENDERER
  if (config.pipeline == SoRenderManager::RenderPipeline::LEGACY_GL) {
    std::cerr << "LegacyGL is unavailable in this build" << std::endl;
    return false;
  }
#endif

  GLTestContextConfig contextConfig;
  contextConfig.profile = config.profile;
  contextConfig.width = config.width;
  contextConfig.height = config.height;
  contextConfig.visible = config.visible;
  contextConfig.vsync = config.vsync;
  if (!impl_->context.initialize(contextConfig)) return false;

  const SbVec2s size(static_cast<short>(config.width),
                     static_cast<short>(config.height));
  const SbViewportRegion viewport(size);
  impl_->manager.setViewportRegion(viewport);
  impl_->manager.setRenderPipeline(config.pipeline);
#if COIN_HAVE_LEGACY_GL_RENDERER
  if (config.pipeline == SoRenderManager::RenderPipeline::LEGACY_GL) {
    impl_->legacyAction = new SoGLRenderAction(viewport);
    impl_->legacyAction->setCacheContext(impl_->context.contextId());
    impl_->legacyAction->setTransparencyType(
      SoGLRenderAction::SORTED_OBJECT_BLEND);
    impl_->manager.setGLRenderAction(impl_->legacyAction);
  }
#endif
  impl_->width = config.width;
  impl_->height = config.height;
  impl_->initialized = true;
  return true;
}

void GLRenderTestSession::shutdown()
{
  impl_->manager.setCamera(nullptr);
  impl_->manager.setSceneGraph(nullptr);
#if COIN_HAVE_LEGACY_GL_RENDERER
  if (impl_->legacyAction) {
    impl_->manager.setGLRenderAction(nullptr);
    delete impl_->legacyAction;
    impl_->legacyAction = nullptr;
  }
#endif
  impl_->context.shutdown();
  impl_->initialized = false;
  impl_->width = 0;
  impl_->height = 0;
}

void GLRenderTestSession::setScene(SoNode * scene, SoCamera * camera)
{
  if (!scene && !camera) {
    impl_->manager.setCamera(nullptr);
    impl_->manager.setSceneGraph(nullptr);
  }
  else {
    impl_->manager.setSceneGraph(scene);
    impl_->manager.setCamera(camera);
  }
}

bool GLRenderTestSession::render(const bool clearColor, const bool clearDepth)
{
  if (!impl_->initialized) return false;
  impl_->context.bindFramebuffer();
  impl_->manager.render(clearColor ? TRUE : FALSE,
                        clearDepth ? TRUE : FALSE);
  return impl_->manager.getLastRenderResult().rendered != FALSE;
}

bool GLRenderTestSession::resize(const int width, const int height)
{
  if (!impl_->initialized ||
      !impl_->context.resizeFramebuffer(width, height)) return false;
  const SbVec2s size(static_cast<short>(width), static_cast<short>(height));
  impl_->manager.setViewportRegion(SbViewportRegion(size));
#if COIN_HAVE_LEGACY_GL_RENDERER
  if (impl_->legacyAction)
    impl_->legacyAction->setViewportRegion(SbViewportRegion(size));
#endif
  impl_->width = width;
  impl_->height = height;
  return true;
}

std::vector<uint8_t> GLRenderTestSession::readPixels() const
{
  return impl_->context.readPixels();
}

bool GLRenderTestSession::initialized() const
{
  return impl_->initialized;
}

GLTestContext & GLRenderTestSession::context()
{
  return impl_->context;
}

SoRenderManager & GLRenderTestSession::manager()
{
  return impl_->manager;
}

const SoRenderManager & GLRenderTestSession::manager() const
{
  return impl_->manager;
}

SoRenderStatistics GLRenderTestSession::statistics() const
{
  return impl_->manager.getRenderStatistics();
}
