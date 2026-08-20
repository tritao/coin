#version 330 core

/* Selection surface root. Match visual coverage, then emit the explicit
 * selection color; selection is interaction state, not scene identity. */
#include "../material/FragmentEvaluation.glsl"
uniform vec4 u_selectionColor;

in vec4 v_color;
in vec3 v_litColor;
in vec2 v_texcoord;

out vec4 fragColor;

void main()
{
  vec4 coverage = coin_surface_fragment_color(v_color, v_litColor, v_texcoord);
  if (!coin_material_alpha_test_pass(coverage.a, u_alphaTestFunction,
                                     u_alphaTestReference)) discard;
  fragColor = u_instanced > 0.5 ? v_color : u_selectionColor;
}
