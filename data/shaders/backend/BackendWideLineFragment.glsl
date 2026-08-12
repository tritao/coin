#version 410 core

uniform float u_stipplePeriod;
uniform int u_alphaTestFunction;
uniform float u_alphaTestReference;

in vec4 v_color;
in float v_lineDistance;

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
  if (u_stipplePeriod > 0.0) {
    if (mod(v_lineDistance, u_stipplePeriod) > u_stipplePeriod * 0.5) discard;
  }

  if (!passesAlphaTest(v_color.a)) discard;
  fragColor = v_color;
}
