#version 330 core

/* Minimal integer-ID root for fully opaque, untextured geometry. */
#include "Peel.glsl"

uniform uint u_pickId;
layout(location = 0) out uint outPickId;

void main()
{
  if (!coin_pick_peel_pass()) discard;
  outPickId = u_pickId;
}
