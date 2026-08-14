#version 330 core

/* Expands points into viewport-sized coverage quads when native point size
 * is insufficient. The generated triangles are implementation detail; the
 * input point remains the semantic primitive. */
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

uniform vec2 u_vpSize;
uniform float u_pointSize;

in vec4 vs_color[];
in vec3 vs_litColor[];
in vec2 vs_texcoord[];
out vec4 v_color;
out vec3 v_litColor;
out vec2 v_texcoord;

void coin_emit_point_corner(vec2 uv)
{
  vec4 center = gl_in[0].gl_Position;
  vec2 pixelOffset = (uv - vec2(0.5)) * u_pointSize;
  vec2 ndcOffset = 2.0 * pixelOffset / u_vpSize;
  gl_Position = center + vec4(ndcOffset * center.w, 0.0, 0.0);
  v_color = vs_color[0];
  v_litColor = vs_litColor[0];
  v_texcoord = vs_texcoord[0];
  EmitVertex();
}

void main()
{
  coin_emit_point_corner(vec2(0.0, 1.0));
  coin_emit_point_corner(vec2(0.0, 0.0));
  coin_emit_point_corner(vec2(1.0, 1.0));
  coin_emit_point_corner(vec2(1.0, 0.0));
  EndPrimitive();
}
