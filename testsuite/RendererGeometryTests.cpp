#define COIN_INTERNAL 1

#include "CoinTest.h"
#include "RendererTestConfig.h"

#include <Inventor/C/tidbits.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoText3.h>
#include <Inventor/rendering/SoRenderIR.h>

#include "base/SbGLUTessellator.h"
#include "fonts/fontlib_wrapper.h"

#include <cstdio>

namespace {

struct TessellationProbe {
  int triangles = 0;
};

void countTessellatedTriangle(void *, void *, void *, void * userdata)
{
  ++static_cast<TessellationProbe *>(userdata)->triangles;
}

const SoRenderCommand *
findGeometryCommand(const SoDrawList & drawlist)
{
  for (int i = 0; i < drawlist.getNumCommands(); ++i) {
    const SoRenderCommand & command = drawlist.getCommand(i);
    if (command.geometry.vertexCount != 0) return &command;
  }
  return nullptr;
}

} // namespace

BOOST_AUTO_TEST_SUITE(RendererGeometryTests);

BOOST_AUTO_TEST_CASE(cpu_concave_polygon_tessellation)
{
  if (!SbGLUTessellator::available()) {
    std::fprintf(stderr, "[SKIP] GLU CPU tessellator unavailable\n");
    return;
  }

  TessellationProbe probe;
  SbGLUTessellator tessellator(countTessellatedTriangle, &probe);
  tessellator.beginPolygon();
  tessellator.addVertex(SbVec3f(0.0f, 0.0f, 0.0f), NULL);
  tessellator.addVertex(SbVec3f(2.0f, 0.0f, 0.0f), NULL);
  tessellator.addVertex(SbVec3f(2.0f, 1.0f, 0.0f), NULL);
  tessellator.addVertex(SbVec3f(1.0f, 1.0f, 0.0f), NULL);
  tessellator.addVertex(SbVec3f(1.0f, 2.0f, 0.0f), NULL);
  tessellator.addVertex(SbVec3f(0.0f, 2.0f, 0.0f), NULL);
  tessellator.endPolygon();

  BOOST_CHECK_EQUAL(probe.triangles, 4);
}

BOOST_AUTO_TEST_CASE(cpu_vector_glyph_generates_retained_commands)
{
#if !COIN_TEST_FONT_AVAILABLE
  std::fprintf(stderr, "[SKIP] no test font was found at configure time\n");
  return;
#else
  if (COIN_TEST_FONT_DIR[0] && !coin_getenv("COIN_FONT_PATH")) {
    coin_setenv("COIN_FONT_PATH", COIN_TEST_FONT_DIR, FALSE);
  }

  const int font = cc_flw_get_font_id(COIN_TEST_FONT_NAME, 32, 0.0f, 0.5f);
  BOOST_REQUIRE(font >= 0);
  cc_flw_ref_font(font);
  const unsigned int glyph = cc_flw_get_glyph(font, 'C');
  struct cc_font_vector_glyph * vector = cc_flw_get_vector_glyph(font, glyph);
  if (!vector) {
    std::fprintf(stderr, "[SKIP] FreeType vector glyph unavailable\n");
    cc_flw_done_glyph(font, glyph);
    cc_flw_unref_font(font);
    return;
  }

  BOOST_REQUIRE(vector->vertices);
  BOOST_REQUIRE(vector->faceindices);
  BOOST_REQUIRE(vector->edgeindices);

  SoSeparator * scene = new SoSeparator;
  scene->ref();
  SoFont * fontNode = new SoFont;
  fontNode->name.setValue(COIN_TEST_FONT_NAME);
  fontNode->size.setValue(32.0f);
  scene->addChild(fontNode);
  SoText3 * text = new SoText3;
  text->string.set1Value(0, "C");
  scene->addChild(text);

  SoIRRenderAction action(SbViewportRegion(32, 32));
  action.apply(scene);
  BOOST_CHECK(findGeometryCommand(action.getDrawList()) != nullptr);
  scene->unref();

  cc_flw_done_glyph(font, glyph);
  cc_flw_unref_font(font);
#endif
}

BOOST_AUTO_TEST_SUITE_END();
