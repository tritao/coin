// Shared state for triangle-to-line/point emulation.  The geometry shader
// must apply culling to the source triangle before it emits coverage quads;
// culling those generated quads would not reproduce polygon culling.

uniform float u_cullBackFaces;
uniform float u_frontFaceCCW;

bool coin_triangle_front_facing()
{
  vec2 p0 = gl_in[0].gl_Position.xy / gl_in[0].gl_Position.w;
  vec2 p1 = gl_in[1].gl_Position.xy / gl_in[1].gl_Position.w;
  vec2 p2 = gl_in[2].gl_Position.xy / gl_in[2].gl_Position.w;
  vec2 d0 = p1 - p0;
  vec2 d1 = p2 - p0;
  bool ccw = d0.x * d1.y - d0.y * d1.x > 0.0;
  return u_frontFaceCCW > 0.5 ? ccw : !ccw;
}

bool coin_triangle_is_culled()
{
  return u_cullBackFaces > 0.5 && !coin_triangle_front_facing();
}
