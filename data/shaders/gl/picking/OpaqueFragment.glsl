#version 330 core

/* Minimal integer-ID root for fully opaque, untextured geometry. */
#include "Peel.glsl"

uniform uint u_pickId;
uniform float u_instanced;
uniform float u_primitivePickIds;
flat in uint v_pickId;
layout(location = 0) out uint outPickId;

void main()
{
  if (!coin_pick_peel_pass()) discard;
  uint baseId = u_instanced > 0.5 ? v_pickId : u_pickId;
  outPickId = baseId + (u_primitivePickIds > 0.5
    ? uint(gl_PrimitiveID) : 0u);
}
