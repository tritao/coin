/*
 * Shared material and texture evaluation for surface fragments.
 *
 * The returned RGBA is the surface coverage used by visual, line, and point
 * roots. Alpha testing remains a caller decision so alternate output roots
 * can share coverage without sharing framebuffer or selection policy.
 */

uniform sampler2D u_texture;
uniform float u_textureEnabled;
uniform int u_textureModel;
uniform vec4 u_textureBlendColor;
uniform vec4 u_color;
uniform float u_useVertexColor;
uniform float u_vertexColorAlphaIncludesOpacity;
uniform float u_textureAlphaIncludesOpacity;
uniform float u_textureHasAlpha;
uniform int u_alphaTestFunction;
uniform float u_alphaTestReference;

#include "Texture.glsl"
#include "AlphaTest.glsl"

vec4
coin_surface_fragment_color(vec4 vertexColor, vec3 litColor, vec2 texcoord)
{
  float materialAlpha = u_color.a;
  if (u_useVertexColor > 0.5 &&
      u_vertexColorAlphaIncludesOpacity > 0.5) {
    materialAlpha = 1.0;
  }
  if (u_textureEnabled > 0.5) {
    return coin_material_textured_color(
      u_texture, texcoord, vec4(litColor, vertexColor.a), materialAlpha,
      u_textureAlphaIncludesOpacity, u_textureHasAlpha, u_textureModel,
      u_textureBlendColor);
  }
  return vec4(litColor, vertexColor.a * materialAlpha);
}
