#version 410 core

uniform float u_renderMode;
uniform sampler2D u_texture;
uniform vec4 u_texModColor;
uniform vec4 u_color;
uniform int u_alphaTestPolicy;
uniform int u_alphaTestFunction;
uniform float u_alphaTestReference;
uniform float u_useVertexColor;
uniform float u_vertexColorAlphaIncludesOpacity;
uniform vec2 u_pixelTextOrigin;
uniform float u_stipplePeriod;

in vec3 v_litColor;
in vec4 v_color;
in float v_vertexAlpha;
in vec2 v_texcoord;
in float v_lineDistance;

out vec4 fragColor;

bool alphaTestPass(float alpha)
{
  if (u_alphaTestPolicy == 0) return true;
  if (u_alphaTestPolicy == 2) return alpha >= u_alphaTestReference;
  if (u_alphaTestPolicy == 3) return alpha > 0.0;

  switch (u_alphaTestFunction) {
    case 1: return false; // NEVER
    case 2: return true;  // ALWAYS
    case 3: return alpha <  u_alphaTestReference;
    case 4: return alpha <= u_alphaTestReference;
    case 5: return alpha == u_alphaTestReference;
    case 6: return alpha >= u_alphaTestReference;
    case 7: return alpha >  u_alphaTestReference;
    case 8: return alpha != u_alphaTestReference;
    default: return true;
  }
}

vec4 composeTexture(vec4 texel)
{
  return vec4(texel.rgb * u_texModColor.rgb,
              texel.a * u_texModColor.a * v_vertexAlpha);
}

void main()
{
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

    vec4 c = vec4(v_color.rgb, v_color.a * materialAlpha);
    if (!alphaTestPass(c.a)) discard;
    fragColor = c;
    return;
  }

  if (u_renderMode > 1.8 && u_renderMode < 2.5) {
    vec4 c = texture(u_texture, v_texcoord);
    c = composeTexture(c);
    if (!alphaTestPass(c.a)) discard;
    fragColor = c;
    return;
  }

  if (u_renderMode > 3.5) {
    ivec2 pixel = ivec2(floor(gl_FragCoord.xy - u_pixelTextOrigin));
    ivec2 textureSizePixels = textureSize(u_texture, 0);
    if (pixel.x < 0 || pixel.y < 0 ||
        pixel.x >= textureSizePixels.x || pixel.y >= textureSizePixels.y) {
      discard;
    }
    vec4 c = texelFetch(u_texture, pixel, 0);
    c = composeTexture(c);
    if (!alphaTestPass(c.a)) discard;
    fragColor = c;
    return;
  }

  if (u_renderMode > 2.5) {
    vec4 c = texture(u_texture, v_texcoord);
    c = composeTexture(c);
    if (!alphaTestPass(c.a)) discard;
    fragColor = c;
    return;
  }

  vec4 c = vec4(v_litColor, v_color.a * materialAlpha);
  if (!alphaTestPass(c.a)) discard;
  fragColor = c;
}
