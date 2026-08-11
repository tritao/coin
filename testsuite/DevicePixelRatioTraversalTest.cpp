#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoDevicePixelRatioElement.h>
#include <Inventor/nodes/SoCallback.h>

#include "support/GLTestContext.h"

namespace {

void
captureDevicePixelRatio(void * userdata, SoAction * action)
{
  float * observed = static_cast<float *>(userdata);
  *observed = SoDevicePixelRatioElement::get(action->getState());
}

}

int
main()
{
  SoDB::init();

  GLTestContext context;
  GLTestContextConfig config;
  config.profile = GLTestProfile::Compatibility;
  if (!context.initialize(config)) {
    SoDB::finish();
    return 77;
  }
  context.makeCurrent();
  context.bindFramebuffer();

  float observed = 0.0f;
  SoCallback * callback = new SoCallback;
  callback->setCallback(captureDevicePixelRatio, &observed);
  callback->ref();

  {
    SoRenderManager manager;
    manager.setSceneGraph(callback);
    manager.setDevicePixelRatio(2.0f);

    SoGLRenderAction action(SbViewportRegion(64, 64));
    manager.render(&action, FALSE, FALSE, FALSE);

    manager.setSceneGraph(NULL);
  }
  callback->unref();
  context.shutdown();
  SoDB::finish();
  return observed == 2.0f ? 0 : 1;
}
