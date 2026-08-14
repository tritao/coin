#version 330 core

/* Visual surface fragment root: shared surface evaluation, explicit alpha
 * test, then framebuffer color. */
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
