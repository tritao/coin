#version 330 core

/* Integer-ID wide-line root. Stipple and shared surface coverage must match
 * the visual wide-line pipeline; only the final output differs. */
#include "../material/FragmentEvaluation.glsl"

uniform int u_stipplePattern;
uniform float u_stippleScale;
uniform uint u_pickId;

in vec4 v_color;
in vec3 v_litColor;
in vec2 v_texcoord;
noperspective in float v_lineDistance;

layout(location = 0) out uint outPickId;

void main()
{
  int bit = int(floor(max(v_lineDistance, 0.0) /
                    max(u_stippleScale, 1.0))) & 15;
  if (((u_stipplePattern >> bit) & 1) == 0) discard;

  vec4 color = coin_surface_fragment_color(v_color, v_litColor, v_texcoord);
  if (!coin_material_alpha_test_pass(color.a, u_alphaTestFunction,
                                     u_alphaTestReference)) discard;
  outPickId = u_pickId;
}
