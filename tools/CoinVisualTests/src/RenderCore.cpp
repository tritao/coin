#include "RenderCore.h"

#include "RenderDriver.h"

#include <Inventor/SbViewportRegion.h>
#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SoPath.h>
#include <Inventor/SbLinear.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/lists/SoPathList.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoSeparator.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include <stb_image_write.h>

namespace CoinVisualTests {

namespace {

bool sceneHasCamera(SoNode* root) {
  if (!root) {
    return false;
  }
  SoSearchAction search;
  search.setType(SoCamera::getClassTypeId());
  search.setSearchingAll(TRUE);
  search.setInterest(SoSearchAction::FIRST);
  search.apply(root);
  return search.getPath() != nullptr;
}

int removeSceneCameras(SoNode* root) {
  if (!root) {
    return 0;
  }
  SoSearchAction search;
  search.setType(SoCamera::getClassTypeId());
  search.setSearchingAll(TRUE);
  search.setInterest(SoSearchAction::ALL);
  search.apply(root);
  const SoPathList& paths = search.getPaths();
  int removed = 0;
  for (size_t idx = 0; idx < paths.getLength(); ++idx) {
    SoPath* path = paths[idx];
    if (!path || path->getLength() < 2) {
      continue;
    }
    SoPath* parent_path = path->copy();
    if (!parent_path) {
      continue;
    }
    parent_path->ref();
    parent_path->truncate(parent_path->getLength() - 1);
    SoNode* parent = parent_path->getTail();
    SoNode* camera = path->getTail();
    if (parent && camera && parent->getTypeId().isDerivedFrom(SoGroup::getClassTypeId())) {
      static_cast<SoGroup*>(parent)->removeChild(camera);
      ++removed;
    }
    parent_path->unref();
  }
  return removed;
}

SoCamera* makeCamera(const CameraSpec& spec) {
  SoCamera* camera = nullptr;
  if (spec.type == CameraSpec::Type::Orthographic) {
    auto* orthographic = new SoOrthographicCamera;
    orthographic->height.setValue(spec.height);
    camera = orthographic;
  } else {
    auto* perspective = new SoPerspectiveCamera;
    const float radians = spec.fov_deg * static_cast<float>(M_PI / 180.0);
    perspective->heightAngle.setValue(radians);
    camera = perspective;
  }
  camera->position.setValue(spec.position);
  camera->pointAt(spec.target, spec.up);
  camera->nearDistance.setValue(spec.near);
  camera->farDistance.setValue(spec.far);
  return camera;
}

} // namespace

struct RenderCore::Impl {
  Options options;
  std::unique_ptr<RenderDriver> driver;
  SoSeparator* scene_root = nullptr;
};

RenderCore::RenderCore() : impl_(new Impl) {}

RenderCore::~RenderCore() {
  if (impl_->scene_root) {
    impl_->scene_root->unref();
    impl_->scene_root = nullptr;
  }
  impl_->driver.reset();
}

bool RenderCore::initialize(const Options& options) {
  impl_->options = options;
  if (options.scene_file.empty() || options.output_file.empty()) {
    std::cerr << "Scene file and output path are required.\n";
    return false;
  }

#if COIN_BUILD_LEGACY_GL_RENDERER
  if (options.renderer == RendererKind::Legacy) {
    impl_->driver = createLegacyGLDriver();
  }
  else
#endif
  {
    if (options.renderer == RendererKind::Legacy) {
      std::cerr << "LegacyGL rendering is unavailable in this build.\n";
      return false;
    }
    impl_->driver = createDrawListDriver(options.gl_profile);
  }
  if (!impl_->driver->initialize(options.width, options.height)) {
    return false;
  }

  SoInput input;
  if (!input.openFile(options.scene_file.c_str())) {
    std::cerr << "Unable to open scene file: " << options.scene_file << '\n';
    return false;
  }
  SoNode* loaded = SoDB::readAll(&input);
  if (!loaded) {
    std::cerr << "Failed to parse scene file: " << options.scene_file << '\n';
    return false;
  }
  loaded->ref();

  impl_->scene_root = new SoSeparator;
  impl_->scene_root->ref();

  if (options.camera_override) {
    const int removed = removeSceneCameras(loaded);
    if (removed > 0 && !options.quiet) {
      std::cerr << "[CoinVisualTests] YAML camera overrides " << removed
                << " camera(s) defined in the scene.\n";
    }
    impl_->scene_root->addChild(makeCamera(options.camera));
  } else if (!sceneHasCamera(loaded)) {
    // Keep scene files authoritative when they provide a camera, but make a
    // camera-less scene useful for a direct snapshot as well.
    CameraSpec fallback;
    impl_->scene_root->addChild(makeCamera(fallback));
  }

  impl_->scene_root->addChild(loaded);
  loaded->unref();
  return true;
}

bool RenderCore::run() {
  if (!impl_->scene_root || !impl_->driver) {
    return false;
  }

  std::vector<uint8_t> pixels;
  const SbViewportRegion viewport(impl_->options.width, impl_->options.height);
  if (!impl_->driver->render(impl_->scene_root,
                             viewport,
                             impl_->options.clear_color,
                             pixels)) {
    return false;
  }

  const int stride = impl_->options.width * 4;
  for (int row = 0; row < impl_->options.height / 2; ++row) {
    auto* top = pixels.data() + static_cast<size_t>(row) * stride;
    auto* bottom = pixels.data() + static_cast<size_t>(impl_->options.height - row - 1) * stride;
    for (int i = 0; i < stride; ++i) {
      std::swap(top[i], bottom[i]);
    }
  }

  if (!stbi_write_png(impl_->options.output_file.c_str(),
                      impl_->options.width,
                      impl_->options.height,
                      4,
                      pixels.data(),
                      stride)) {
    std::cerr << "Failed to write PNG output: " << impl_->options.output_file << '\n';
    return false;
  }
  if (!impl_->options.quiet) {
    std::cout << "Rendered scene to " << impl_->options.output_file << '\n';
  }
  return true;
}

} // namespace CoinVisualTests
