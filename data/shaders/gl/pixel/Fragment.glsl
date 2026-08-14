#version 330 core

/* Pixel fragment root. Pixel texels are final producer RGBA; only explicit
 * pixel alpha-test state determines whether a covered fragment survives. */
uniform sampler2D u_texture;
uniform int u_alphaTestFunction;
uniform float u_alphaTestReference;
uniform vec2 u_sourceSize;
uniform vec2 u_rasterSize;
uniform vec2 u_pixelOrigin;
uniform vec2 u_viewportOrigin;

out vec4 fragColor;

#include "Common.glsl"
#include "../material/AlphaTest.glsl"

void main()
{
  vec2 rasterPixel = coin_pixel_raster_coordinate(
    gl_FragCoord.xy, u_viewportOrigin, u_pixelOrigin);
  if (!coin_pixel_in_raster(rasterPixel, u_rasterSize)) {
    discard;
  }
  ivec2 sourcePixel = coin_pixel_source_coordinate(
    rasterPixel, u_sourceSize, u_rasterSize, textureSize(u_texture, 0));
  vec4 texel = coin_pixel_fetch(u_texture, sourcePixel);
  // Pixel producers retain final RGBA.  Unlike ordinary surface commands,
  // image/text pixels must not be modulated by inherited material state.
  if (!coin_material_alpha_test_pass(texel.a, u_alphaTestFunction,
                                     u_alphaTestReference)) discard;
  fragColor = texel;
}
