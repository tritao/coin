#version 410 core

const int COIN_MAX_LIGHTS = 8;

uniform float u_renderMode;
uniform sampler2D u_texture;
uniform vec4 u_texModColor;
uniform vec4 u_color;
uniform float u_useVertexColor;
uniform float u_vertexColorAlphaIncludesOpacity;
uniform float u_stipplePeriod;
uniform float u_metalness;
uniform float u_roughness;

in vec3 v_litColor;
in vec4 v_color;
in float v_vertexAlpha;
in vec2 v_texcoord;
in vec2 v_winPos;
in float v_lineDistance;

out vec4 fragColor;

vec4 composeTexture(vec4 texel)
{
  return vec4(texel.rgb * u_texModColor.rgb,
              texel.a * u_texModColor.a * v_vertexAlpha);
}

void main()
{
  // A generated per-vertex color may carry the same material opacity as the
  // command. Apply that opacity here unless the producer explicitly says the
  // vertex alpha already includes it.
  float materialAlpha = (u_useVertexColor > 0.5 &&
                         u_vertexColorAlphaIncludesOpacity < 0.5)
    ? u_color.a : 1.0;

  if (u_renderMode > 0.5 && u_renderMode < 1.8) {
    if (u_renderMode > 1.2) {
      vec2 pc = gl_PointCoord - vec2(0.5);
      if (dot(pc, pc) > 0.25) discard;
    }

    if (u_stipplePeriod > 0.0) {
      if (mod(v_lineDistance, u_stipplePeriod) > u_stipplePeriod * 0.5) discard;
    }

    fragColor = vec4(v_color.rgb, v_color.a * materialAlpha);
    return;
  }

  if (u_renderMode > 1.8 && u_renderMode < 2.5) {
    vec4 c = texture(u_texture, v_texcoord);
    if (c.a < 0.3) discard;
    fragColor = composeTexture(c);
    return;
  }

  if (u_renderMode > 2.5) {
    vec4 c = texture(u_texture, v_texcoord);
    // World-space textured annotations (e.g. SoDatumLabel) use normal
    // blending, so preserve their antialiased glyph edge pixels. Billboard
    // text above retains the legacy alpha-test threshold.
    if (c.a <= 0.0) discard;
    fragColor = composeTexture(c);
    return;
  }

  fragColor = vec4(v_litColor, v_color.a * materialAlpha);
}
