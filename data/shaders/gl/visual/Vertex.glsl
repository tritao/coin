#version 330 core

/* Visual surface vertex root. The backend requires OpenGL 3.3 or newer, so
 * executable roots use GLSL 330 core; this root wires shared vertex color and
 * lighting evaluation into the ordinary triangle pipeline. */
#include "../material/VertexEvaluation.glsl"
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec2 a_texcoord;
layout(location = 5) in vec4 a_instanceModel0;
layout(location = 6) in vec4 a_instanceModel1;
layout(location = 7) in vec4 a_instanceModel2;
layout(location = 8) in vec4 a_instanceModel3;
layout(location = 9) in vec4 a_instanceColor;
layout(location = 10) in uint a_instancePickId;

uniform float u_instanced;

out vec4 v_color;
out vec3 v_litColor;
out vec2 v_texcoord;
flat out uint v_pickId;

void main()
{
  v_color = u_instanced > 0.5 && u_useVertexColor < 0.5
    ? a_instanceColor : coin_surface_vertex_color(a_color);
  v_texcoord = a_texcoord;
  v_pickId = a_instancePickId;

  mat4 instanceModel = mat4(a_instanceModel0, a_instanceModel1,
                            a_instanceModel2, a_instanceModel3);
  mat4 model = u_instanced > 0.5 ? instanceModel : u_model;
  vec4 worldPos = model * vec4(a_position, 1.0);
  vec4 eyePos = u_view * worldPos;
  mat3 normalMatrix = transpose(inverse(mat3(u_view * model)));
  vec3 eyeNormal = normalMatrix * a_normal;
  v_litColor = coin_surface_lit_color(v_color, eyePos.xyz, eyeNormal);
  gl_Position = u_proj * eyePos;
}
