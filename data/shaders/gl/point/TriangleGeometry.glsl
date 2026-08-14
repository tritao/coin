#version 330 core

/* Expands triangle point coverage into one quad per source vertex after
 * source-facing and strip-parity decisions. */
layout(triangles) in;
layout(triangle_strip, max_vertices = 12) out;

uniform vec2 u_vpSize;
uniform float u_pointSize;

#include "../visual/TriangleFallback.glsl"

in vec4 vs_color[];
in vec3 vs_litColor[];
in vec2 vs_texcoord[];
out vec4 v_color;
out vec3 v_litColor;
out vec2 v_texcoord;

void coin_emit_point_corner(int index, vec2 uv)
{
  vec4 center = gl_in[index].gl_Position;
  vec2 pixelOffset = (uv - vec2(0.5)) * u_pointSize;
  vec2 ndcOffset = 2.0 * pixelOffset / u_vpSize;
  gl_Position = center + vec4(ndcOffset * center.w, 0.0, 0.0);
  v_color = vs_color[index];
  v_litColor = vs_litColor[index];
  v_texcoord = vs_texcoord[index];
  EmitVertex();
}

void coin_emit_point(int index)
{
  coin_emit_point_corner(index, vec2(0.0, 1.0));
  coin_emit_point_corner(index, vec2(0.0, 0.0));
  coin_emit_point_corner(index, vec2(1.0, 1.0));
  coin_emit_point_corner(index, vec2(1.0, 0.0));
  EndPrimitive();
}

void main()
{
  if (coin_triangle_is_culled()) return;
  coin_emit_point(0);
  coin_emit_point(1);
  coin_emit_point(2);
}
