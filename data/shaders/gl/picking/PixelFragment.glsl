#version 330 core

/* Integer-ID pixel root. Pixel producers supply final RGBA, so this pass
 * applies only pixel-coordinate coverage and explicit alpha testing before
 * writing the uint identity. */
uniform sampler2D u_texture;
uniform int u_alphaTestFunction;
uniform float u_alphaTestReference;
uniform vec2 u_sourceSize;
uniform vec2 u_rasterSize;
uniform vec2 u_pixelOrigin;
uniform vec2 u_viewportOrigin;
uniform uint u_pickId;

in vec2 v_texcoord;
layout(location = 0) out uint outPickId;

#include "../pixel/Common.glsl"
#include "../material/AlphaTest.glsl"
#include "Peel.glsl"

void main()
{
  if (!coin_pick_peel_pass()) discard;
  vec2 rasterPixel = coin_pixel_raster_coordinate(
    gl_FragCoord.xy, u_viewportOrigin, u_pixelOrigin);
  if (!coin_pixel_in_raster(rasterPixel, u_rasterSize)) discard;
  ivec2 pixel = coin_pixel_source_coordinate(
    rasterPixel, u_sourceSize, u_rasterSize, textureSize(u_texture, 0));
  vec4 texel = coin_pixel_fetch(u_texture, pixel);
  if (!coin_material_alpha_test_pass(texel.a, u_alphaTestFunction,
                                     u_alphaTestReference)) discard;
  outPickId = u_pickId;
}
