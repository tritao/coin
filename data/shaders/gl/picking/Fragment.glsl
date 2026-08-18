#version 330 core

/* Integer-ID surface root. Reuse visual coverage and alpha testing, then
 * replace visual output with the frame-local uint identity. */
#include "../material/FragmentEvaluation.glsl"
#include "Peel.glsl"

uniform uint u_pickId;

in vec4 v_color;
in vec3 v_litColor;
in vec2 v_texcoord;

layout(location = 0) out uint outPickId;

void main()
{
  if (!coin_pick_peel_pass()) discard;
  vec4 color = coin_surface_fragment_color(v_color, v_litColor, v_texcoord);
  if (!coin_material_alpha_test_pass(color.a, u_alphaTestFunction,
                                     u_alphaTestReference)) discard;
  outPickId = u_pickId;
}
