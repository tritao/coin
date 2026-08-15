#version 330 core

/* Selection pixel root. Match final-RGBA pixel coverage exactly, then emit
 * the requested overlay color. */
uniform sampler2D u_texture;
uniform int u_alphaTestFunction;
uniform float u_alphaTestReference;
uniform vec2 u_sourceSize;
uniform vec2 u_rasterSize;
uniform vec2 u_pixelOrigin;
uniform vec2 u_viewportOrigin;
uniform vec4 u_selectionColor;

in vec2 v_texcoord;
out vec4 fragColor;

#include "../pixel/Common.glsl"
#include "../material/AlphaTest.glsl"

void main()
{
  vec2 rasterPixel = coin_pixel_raster_coordinate(
    gl_FragCoord.xy, u_viewportOrigin, u_pixelOrigin);
  if (!coin_pixel_in_raster(rasterPixel, u_rasterSize)) discard;
  ivec2 pixel = coin_pixel_source_coordinate(
    rasterPixel, u_sourceSize, u_rasterSize, textureSize(u_texture, 0));
  vec4 texel = coin_pixel_fetch(u_texture, pixel);
  if (!coin_material_alpha_test_pass(texel.a, u_alphaTestFunction,
                                     u_alphaTestReference)) discard;
  fragColor = u_selectionColor;
}
