/* Shared bounded depth-peeling rejection for integer-ID picking. */
uniform sampler2D u_previousDepth;
uniform int u_peelEnabled;

bool coin_pick_peel_pass()
{
  if (u_peelEnabled == 0) return true;
  float previous = texelFetch(u_previousDepth, ivec2(gl_FragCoord.xy), 0).r;
  return gl_FragCoord.z > previous + 1.0e-6;
}
