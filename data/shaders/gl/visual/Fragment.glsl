#version 330 core

/* Base retained visual fragment root: evaluate the bound surface and emit
 * framebuffer color. Coverage policy remains explicit at this root. */
#include "../common/VertexColor.glsl"

uniform sampler2D u_texture;
uniform float u_textureEnabled;
uniform vec4 u_texModColor;

in vec4 v_color;
in vec2 v_texcoord;

out vec4 fragColor;

void main()
{
  fragColor = coin_visual_texture(v_color, u_texture, v_texcoord,
                                  u_textureEnabled, u_texModColor);
}
