#version 410 core

in vec4 v_color;
in vec2 v_uv;

uniform float u_pointSize;
uniform float u_roundPoints;
uniform int u_alphaTestFunction;
uniform float u_alphaTestReference;

out vec4 fragColor;

bool passesAlphaTest(float alpha)
{
  switch (u_alphaTestFunction) {
  case 1: return false;
  case 2: return true;
  case 3: return alpha < u_alphaTestReference;
  case 4: return alpha <= u_alphaTestReference;
  case 5: return abs(alpha - u_alphaTestReference) < 0.0001;
  case 6: return alpha >= u_alphaTestReference;
  case 7: return alpha > u_alphaTestReference;
  case 8: return abs(alpha - u_alphaTestReference) >= 0.0001;
  default: return true;
  }
}

void main()
{
  if (u_roundPoints > 0.5 && u_pointSize > 2.5) {
    vec2 delta = v_uv - vec2(0.5);
    if (dot(delta, delta) > 0.25) discard;
  }
  if (!passesAlphaTest(v_color.a)) discard;
  fragColor = v_color;
}
