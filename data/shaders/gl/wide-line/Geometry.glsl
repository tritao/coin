#version 330 core

/* Expands retained lines into raster triangles when native line width is
 * insufficient. Line distance is carried per source occurrence so indexed
 * repeated vertices retain independent stipple progression. */
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;

uniform vec2 u_vpSize;
uniform float u_lineWidth;

in vec4 vs_color[];
in vec3 vs_litColor[];
in vec2 vs_texcoord[];
noperspective in float vs_lineDistance[];

out vec4 v_color;
out vec3 v_litColor;
out vec2 v_texcoord;
noperspective out float v_lineDistance;

void coin_emit_line_vertex(int index, vec2 offset)
{
  vec4 position = gl_in[index].gl_Position;
  v_color = vs_color[index];
  v_litColor = vs_litColor[index];
  v_texcoord = vs_texcoord[index];
  v_lineDistance = vs_lineDistance[index];
  gl_Position = position + vec4(offset * position.w, 0.0, 0.0);
  EmitVertex();
}

void main()
{
  vec4 p0 = gl_in[0].gl_Position;
  vec4 p1 = gl_in[1].gl_Position;
  vec2 ndc0 = p0.xy / p0.w;
  vec2 ndc1 = p1.xy / p1.w;
  vec2 delta = ndc1 - ndc0;
  vec2 dir = length(delta) > 0.000001 ? normalize(delta) : vec2(1.0, 0.0);
  vec2 perp = vec2(-dir.y, dir.x);
  vec2 offset = perp * u_lineWidth / u_vpSize;

  coin_emit_line_vertex(0, offset);
  coin_emit_line_vertex(0, -offset);
  coin_emit_line_vertex(1, offset);
  coin_emit_line_vertex(1, -offset);
  EndPrimitive();
}
