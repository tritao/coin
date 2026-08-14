#version 330 core

/* Visual surface vertex root. The backend requires OpenGL 3.3 or newer, so
 * executable roots use GLSL 330 core; this root wires shared vertex color and
 * lighting evaluation into the ordinary triangle pipeline. */
#include "../material/VertexEvaluation.glsl"
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec2 a_texcoord;

out vec4 v_color;
out vec3 v_litColor;
out vec2 v_texcoord;

void main()
{
  v_color = coin_surface_vertex_color(a_color);
  v_texcoord = a_texcoord;

  vec4 worldPos = u_model * vec4(a_position, 1.0);
  vec4 eyePos = u_view * worldPos;
  mat3 normalMatrix = transpose(inverse(mat3(u_view * u_model)));
  vec3 eyeNormal = normalMatrix * a_normal;
  v_litColor = coin_surface_lit_color(v_color, eyePos.xyz, eyeNormal);
  gl_Position = u_proj * eyePos;
}
