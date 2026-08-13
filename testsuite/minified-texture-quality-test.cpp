#include "rendering/CoinOffscreenGLCanvas.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/system/gl.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

constexpr int WIDTH = 512;
constexpr int HEIGHT = 512;

int skip(const char * reason)
{
  std::cout << "SKIP: " << reason << std::endl;
  return 77;
}

void set_environment(const char * name, const char * value)
{
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

SoSeparator * makeMinifiedLabelScene()
{
  SoSeparator * root = new SoSeparator;

  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0.0f, 0.0f, 4.0f);
  camera->nearDistance = 0.1f;
  camera->farDistance = 10.0f;
  // Model a fixed-resolution label texture on a small chamfered face overlay.
  // The 1.52-unit inset occupies about 48px at this target size.
  camera->height = 16.2f;
  root->addChild(camera);

  SoComplexity * complexity = new SoComplexity;
  complexity->textureQuality = 1.0f;
  root->addChild(complexity);

  SoLightModel * lightModel = new SoLightModel;
  lightModel->model = SoLightModel::BASE_COLOR;
  root->addChild(lightModel);

  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(1.0f, 1.0f, 1.0f);
  root->addChild(material);

  const int textureSize = 192;
  std::vector<unsigned char> pixels(textureSize * textureSize * 4);
  for (int y = 0; y < textureSize; ++y) {
    for (int x = 0; x < textureSize; ++x) {
      const unsigned char value = ((x ^ y) & 1) ? 255 : 24;
      const size_t offset = static_cast<size_t>(y * textureSize + x) * 4;
      pixels[offset + 0] = value;
      pixels[offset + 1] = value;
      pixels[offset + 2] = value;
      pixels[offset + 3] = 255;
    }
  }

  SoTexture2 * texture = new SoTexture2;
  texture->model = SoTexture2::MODULATE;
  texture->image.setValue(SbVec2s(textureSize, textureSize), 4, pixels.data());
  root->addChild(texture);

  // A fixed-resolution texture mapped onto the inset of a unit face with a
  // 0.12 chamfer, representative of a small overlay label.
  const float inset = 1.0f - 2.0f * 0.12f;
  const SbVec3f points[] = {
    SbVec3f(-inset, -inset, 1.0f),
    SbVec3f(inset, -inset, 1.0f),
    SbVec3f(inset, inset, 1.0f),
    SbVec3f(-inset, inset, 1.0f)
  };
  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.setValues(0, 4, points);
  root->addChild(coordinates);

  const SbVec2f texcoordPoints[] = {
    SbVec2f(0.0f, 0.0f),
    SbVec2f(1.0f, 0.0f),
    SbVec2f(1.0f, 1.0f),
    SbVec2f(0.0f, 1.0f)
  };
  SoTextureCoordinate2 * texcoords = new SoTextureCoordinate2;
  texcoords->point.setValues(0, 4, texcoordPoints);
  root->addChild(texcoords);

  SoFaceSet * faces = new SoFaceSet;
  faces->numVertices = 4;
  root->addChild(faces);
  return root;
}

std::vector<unsigned char> readPixels(CoinOffscreenGLCanvas & canvas)
{
  glFinish();
  std::vector<unsigned char> pixels(WIDTH * HEIGHT * 4, 0);
  canvas.readPixels(pixels.data(), SbVec2s(WIDTH, HEIGHT), WIDTH, 4);
  return pixels;
}

struct RegionStats {
  int objectPixels = 0;
  int brightPixels = 0;
  int minValue = 255;
  int maxValue = 0;
  double mean = 0.0;
};

RegionStats stats(const std::vector<unsigned char> & pixels)
{
  RegionStats result;
  // Stay well inside the roughly 48px label quad, excluding its boundary and
  // the clear color around the face.
  for (int y = HEIGHT / 2 - 16; y < HEIGHT / 2 + 16; ++y) {
    for (int x = WIDTH / 2 - 16; x < WIDTH / 2 + 16; ++x) {
      const unsigned char * pixel = &pixels[(y * WIDTH + x) * 4];
      const int value = pixel[0];
      result.minValue = std::min(result.minValue, value);
      result.maxValue = std::max(result.maxValue, value);
      result.mean += value;
      ++result.objectPixels;
      if (value > 100) ++result.brightPixels;
    }
  }
  result.mean /= result.objectPixels;
  return result;
}

}

int main()
{
  set_environment("COIN_EGL", "1");
  set_environment("EGL_PLATFORM", "surfaceless");
  set_environment("COIN_EGL_CORE_PROFILE", "1");

  SoDB::init();

  int result = 0;
  {
    CoinOffscreenGLCanvas canvas;
    canvas.setWantedSize(SbVec2s(WIDTH, HEIGHT));
    if (canvas.activateGLContext() == 0) {
      result = skip("core EGL offscreen context is unavailable");
    }
    else {
      SoSeparator * scene = makeMinifiedLabelScene();
      scene->ref();

      std::vector<unsigned char> drawListPixels;
      {
        SoRenderManager manager;
        SbViewportRegion viewport(SbVec2s(WIDTH, HEIGHT));
        viewport.setViewportPixels(SbVec2s(0, 0), SbVec2s(WIDTH, HEIGHT));
        manager.setViewportRegion(viewport);
        manager.setSceneGraph(scene);
        manager.setCamera(NULL);
        manager.setLightingMode(SoRenderManager::UNLIT);
        manager.setBackgroundColor(SbColor4f(0.12f, 0.12f, 0.12f, 1.0f));

        manager.setRenderPipeline(SoRenderManager::RenderPipeline::DRAW_LIST);
        manager.render(TRUE, TRUE);
        drawListPixels = readPixels(canvas);
      }

      const RegionStats drawList = stats(drawListPixels);
      if (drawList.brightPixels < 500) {
        std::cerr << "FAIL: minified texture disappeared in DrawList (bright="
                  << drawList.brightPixels << ")" << std::endl;
        result = 1;
      }
      if (drawList.minValue < 70 || drawList.maxValue - drawList.minValue > 180) {
        std::cerr << "FAIL: minified checker did not resolve to a filtered color"
                  << " (DrawList range=" << drawList.minValue << ".." << drawList.maxValue
                  << ")" << std::endl;
        result = 1;
      }
      if (drawList.mean < 110.0 || drawList.mean > 170.0) {
        std::cerr << "FAIL: minified checker produced an unexpected DrawList mean "
                  << drawList.mean << std::endl;
        result = 1;
      }

      scene->unref();
      canvas.deactivateGLContext();
    }
  }

  SoDB::finish();
  return result;
}
