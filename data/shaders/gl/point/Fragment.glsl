#version 330 core

/* Point fragment root: generated point coverage shares surface evaluation
 * with the ordinary visual pipeline and keeps alpha testing explicit. */
#include "../material/FragmentEvaluation.glsl"

in vec4 v_color;
in vec3 v_litColor;
in vec2 v_texcoord;
out vec4 fragColor;

void main()
{
  vec4 color = coin_surface_fragment_color(v_color, v_litColor, v_texcoord);
  if (!coin_material_alpha_test_pass(color.a, u_alphaTestFunction,
                                     u_alphaTestReference)) discard;
  fragColor = color;
}
