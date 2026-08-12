#version 410 core

const int COIN_MAX_LIGHTS = 8;

uniform float u_renderMode;
uniform sampler2D u_texture;
uniform vec4 u_texModColor;
uniform vec2 u_pixelOrigin;
uniform vec4 u_color;
uniform float u_useVertexColor;
uniform float u_vertexColorAlphaIncludesOpacity;
uniform float u_textureAlphaIncludesOpacity;
uniform int u_alphaTestFunction;
uniform float u_alphaTestReference;
uniform float u_stipplePeriod;

in vec3 v_litColor;
in vec4 v_color;
in float v_vertexAlpha;
in vec2 v_texcoord;
in vec2 v_winPos;
in float v_lineDistance;

out vec4 fragColor;

vec4 composeTexture(vec4 texel)
{
  bool hasVertexColor = u_useVertexColor > 0.5;
  bool vertexColorCarriesMaterial =
    hasVertexColor && u_vertexColorAlphaIncludesOpacity > 0.5;
  vec3 materialColor = hasVertexColor ? v_color.rgb : u_texModColor.rgb;
  float materialAlpha = hasVertexColor
    ? (vertexColorCarriesMaterial ? 1.0 : u_color.a)
    : u_texModColor.a;
  float vertexAlpha = hasVertexColor ? v_vertexAlpha : 1.0;
  float textureAlpha = u_textureAlphaIncludesOpacity > 0.5
    ? 1.0 : materialAlpha;
  return vec4(texel.rgb * materialColor,
              texel.a * textureAlpha * vertexAlpha);
}

bool passesAlphaTest(float alpha)
{
  switch (u_alphaTestFunction) {
  case 1: return false; // NEVER
  case 2: return true;  // ALWAYS
  case 3: return alpha < u_alphaTestReference;
  case 4: return alpha <= u_alphaTestReference;
  case 5: return abs(alpha - u_alphaTestReference) < 0.0001;
  case 6: return alpha >= u_alphaTestReference;
  case 7: return alpha > u_alphaTestReference;
  case 8: return abs(alpha - u_alphaTestReference) >= 0.0001;
  default: return true; // NONE
  }
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

    vec4 outputColor = vec4(v_color.rgb, v_color.a * materialAlpha);
    if (!passesAlphaTest(outputColor.a)) discard;
    fragColor = outputColor;
    return;
  }

  if (u_renderMode > 1.8 && u_renderMode < 2.5) {
    vec4 c = texture(u_texture, v_texcoord);
    if (u_alphaTestFunction == 0 && c.a < 0.3) discard;
    vec4 outputColor = composeTexture(c);
    if (!passesAlphaTest(outputColor.a)) discard;
    fragColor = outputColor;
    return;
  }

  if (u_renderMode > 3.5) {
    ivec2 pixel = ivec2(floor(gl_FragCoord.xy - u_pixelOrigin));
    ivec2 textureSizePixels = textureSize(u_texture, 0);
    if (pixel.x < 0 || pixel.y < 0 ||
        pixel.x >= textureSizePixels.x || pixel.y >= textureSizePixels.y) {
      discard;
    }
    vec4 c = texelFetch(u_texture, pixel, 0);
    if (c.a < 0.3) discard;
    fragColor = composeTexture(c);
    return;
  }

  if (u_renderMode > 2.5) {
    vec4 c = texture(u_texture, v_texcoord);
    // World-space textured annotations (e.g. SoDatumLabel) use normal
    // blending, so preserve their antialiased glyph edge pixels. Billboard
    // text above retains the legacy alpha-test threshold.
    if (u_alphaTestFunction == 0 && c.a <= 0.0) discard;
    vec4 outputColor = composeTexture(c);
    if (!passesAlphaTest(outputColor.a)) discard;
    fragColor = outputColor;
    return;
  }

  vec4 outputColor = vec4(v_litColor, v_color.a * materialAlpha);
  if (!passesAlphaTest(outputColor.a)) discard;
  fragColor = outputColor;
}
