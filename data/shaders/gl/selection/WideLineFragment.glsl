#version 330 core

/* Selection wide-line root. Preserve stipple and coverage while changing
 * only the output color used by the explicit overlay operation. */
#include "../material/FragmentEvaluation.glsl"

uniform int u_stipplePattern;
uniform float u_stippleScale;
uniform vec4 u_selectionColor;

in vec4 v_color;
in vec3 v_litColor;
in vec2 v_texcoord;
noperspective in float v_lineDistance;

out vec4 fragColor;

void main()
{
  int bit = int(floor(max(v_lineDistance, 0.0) /
                    max(u_stippleScale, 1.0))) & 15;
  if (((u_stipplePattern >> bit) & 1) == 0) discard;

  vec4 coverage = coin_surface_fragment_color(v_color, v_litColor, v_texcoord);
  if (!coin_material_alpha_test_pass(coverage.a, u_alphaTestFunction,
                                     u_alphaTestReference)) discard;
  fragColor = u_selectionColor;
}
