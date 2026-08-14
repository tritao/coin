#version 330 core

/*
 * Base retained visual vertex root. The OpenGL backend requires 3.3 or newer;
 * executable roots therefore use GLSL 330 core. Included files are semantic
 * modules and intentionally provide no version directive or main().
 */
#include "../common/VertexColor.glsl"

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_texcoord;

uniform mat4 u_proj;
uniform mat4 u_view;
uniform mat4 u_model;
uniform vec4 u_color;
uniform float u_useVertexColor;

out vec4 v_color;
out vec2 v_texcoord;

void main()
{
  v_color = (u_useVertexColor > 0.5) ? a_color : u_color;
  v_texcoord = a_texcoord;
  gl_Position = u_proj * u_view * u_model * vec4(a_position, 1.0);
}
