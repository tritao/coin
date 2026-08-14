/*
 * Shared surface evaluation for visual, wide-line, and point vertex stages.
 *
 * This module selects vertex color and computes optional view-space lighting.
 * It owns no framebuffer output, raster-path selection, pass ordering, or
 * alpha-test decision.
 */

#include "../common/VertexColor.glsl"

uniform mat4 u_proj;
uniform mat4 u_view;
uniform mat4 u_model;
uniform vec4 u_color;
uniform float u_useVertexColor;
uniform int u_shadingModel;
uniform vec3 u_emissiveColor;
uniform vec3 u_ambientLight;
uniform vec3 u_materialAmbient;
uniform vec3 u_materialSpecular;
uniform float u_materialShininess;
uniform float u_twoSidedLighting;
uniform int u_lightCount;
uniform int u_lightType[8];
uniform vec3 u_lightColor[8];
uniform vec3 u_lightDirection[8];
uniform vec3 u_lightPosition[8];
uniform vec3 u_lightAttenuation[8];
uniform vec2 u_lightSpotParams[8];

#include "Lighting.glsl"

vec4
coin_surface_vertex_color(vec4 vertexColor)
{
  return coin_visual_color(vertexColor, vec4(u_color.rgb, 1.0),
                           u_useVertexColor);
}

vec3
coin_surface_lit_color(vec4 vertexColor, vec3 eyePosition, vec3 eyeNormal)
{
  return u_shadingModel == 0
    ? vertexColor.rgb
    : coin_material_compute_gouraud_color(eyePosition, eyeNormal,
                                          vertexColor.rgb);
}
