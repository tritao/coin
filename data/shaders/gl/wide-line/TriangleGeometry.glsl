#version 330 core

/* Expands triangle wireframe edges into independent coverage quads. Source
 * facing is decided before emission and each edge computes its own pattern
 * distance. */
layout(triangles) in;
layout(triangle_strip, max_vertices = 12) out;

uniform vec2 u_vpSize;
uniform float u_lineWidth;

#include "../visual/TriangleFallback.glsl"

in vec4 vs_color[];
in vec3 vs_litColor[];
in vec2 vs_texcoord[];
noperspective in float vs_lineDistance[];
out vec4 v_color;
out vec3 v_litColor;
out vec2 v_texcoord;
noperspective out float v_lineDistance;

void coin_emit_edge_vertex(int index, vec2 offset, float lineDistance)
{
  vec4 position = gl_in[index].gl_Position;
  v_color = vs_color[index];
  v_litColor = vs_litColor[index];
  v_texcoord = vs_texcoord[index];
  v_lineDistance = lineDistance;
  gl_Position = position + vec4(offset * position.w, 0.0, 0.0);
  EmitVertex();
}

void coin_emit_edge(int first, int second)
{
  vec4 p0 = gl_in[first].gl_Position;
  vec4 p1 = gl_in[second].gl_Position;
  vec2 ndc0 = p0.xy / p0.w;
  vec2 ndc1 = p1.xy / p1.w;
  vec2 delta = ndc1 - ndc0;
  float edgeLength = length(delta * (0.5 * u_vpSize));
  vec2 dir = length(delta) > 0.000001 ? normalize(delta) : vec2(1.0, 0.0);
  vec2 offset = vec2(-dir.y, dir.x) * u_lineWidth / u_vpSize;
  coin_emit_edge_vertex(first, offset, 0.0);
  coin_emit_edge_vertex(first, -offset, 0.0);
  coin_emit_edge_vertex(second, offset, edgeLength);
  coin_emit_edge_vertex(second, -offset, edgeLength);
  EndPrimitive();
}

void main()
{
  if (coin_triangle_is_culled()) return;
  coin_emit_edge(0, 1);
  coin_emit_edge(1, 2);
  coin_emit_edge(2, 0);
}
