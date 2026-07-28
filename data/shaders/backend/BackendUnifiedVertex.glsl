#version 410 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec2 a_texcoord;
layout(location = 4) in float a_lineDistance;

uniform mat4 u_proj;
uniform mat4 u_view;
uniform mat4 u_model;
uniform vec4 u_color;
uniform float u_useVertexColor;
uniform float u_renderMode;
uniform vec3 u_quadCenter;
uniform vec2 u_texSize;
uniform vec2 u_vpSize;

out vec3 v_eyePos;
out vec3 v_eyeNormal;
out vec4 v_color;
out vec2 v_texcoord;
out vec2 v_winPos;
out float v_lineDistance;

void main()
{
  v_color = (u_useVertexColor > 0.5) ? a_color : u_color;
  v_texcoord = a_texcoord;

  if (u_renderMode > 1.5 && u_renderMode < 2.5) {
    vec4 centerClip = u_proj * u_view * u_model * vec4(u_quadCenter, 1.0);
    vec2 pixelOffset = (a_texcoord - vec2(0.5)) * u_texSize;
    vec2 ndcOffset = 2.0 * pixelOffset / u_vpSize;
    gl_Position = centerClip + vec4(ndcOffset * centerClip.w, 0.0, 0.0);
    v_eyePos = vec3(0.0);
    v_eyeNormal = vec3(0.0, 0.0, 1.0);
    v_winPos = vec2(0.0);
    v_lineDistance = 0.0;
  }
  else {
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    vec4 eyePos = u_view * worldPos;
    v_eyePos = eyePos.xyz;
    v_eyeNormal = mat3(u_view) * mat3(u_model) * a_normal;
    gl_Position = u_proj * eyePos;
    v_winPos = (gl_Position.xy / gl_Position.w + 1.0) * 0.5 * u_vpSize;
    v_lineDistance = a_lineDistance;
  }
}
