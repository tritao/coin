#version 410 core
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;

uniform vec2 uVpSize;
uniform float uLineWidth;

in vec4 vIdColor[];
out vec4 gIdColor;

void main()
{
  vec4 p0 = gl_in[0].gl_Position;
  vec4 p1 = gl_in[1].gl_Position;
  vec2 ndc0 = p0.xy / p0.w;
  vec2 ndc1 = p1.xy / p1.w;
  vec2 dir = normalize(ndc1 - ndc0);
  vec2 perp = vec2(-dir.y, dir.x);
  vec2 offset = perp * uLineWidth / uVpSize;

  gIdColor = vIdColor[0];
  gl_Position = p0 + vec4(offset * p0.w, 0.0, 0.0);
  EmitVertex();
  gl_Position = p0 - vec4(offset * p0.w, 0.0, 0.0);
  EmitVertex();

  gIdColor = vIdColor[1];
  gl_Position = p1 + vec4(offset * p1.w, 0.0, 0.0);
  EmitVertex();
  gl_Position = p1 - vec4(offset * p1.w, 0.0, 0.0);
  EmitVertex();

  EndPrimitive();
}
