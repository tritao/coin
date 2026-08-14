/*
 * Retained material texture evaluation.
 *
 * Applies Coin's texture model and alpha semantics to a supplied surface
 * color. It does not choose a render pass or perform alpha testing; callers
 * retain those decisions explicitly so visual, picking, and selection roots
 * can share coverage without sharing output policy.
 */

const int COIN_TEXTURE_MODEL_MODULATE = 0;
const int COIN_TEXTURE_MODEL_DECAL = 1;
const int COIN_TEXTURE_MODEL_BLEND = 2;
const int COIN_TEXTURE_MODEL_REPLACE = 3;

vec4 coin_material_textured_color(sampler2D textureSampler, vec2 texcoord,
                                  vec4 visualColor, float materialAlpha,
                                  float textureAlphaIncludesOpacity,
                                  float textureHasAlpha,
                                  int textureModel, vec4 blendColor)
{
  vec4 texel = texture(textureSampler, texcoord);
  float primaryAlpha = visualColor.a *
    (textureAlphaIncludesOpacity > 0.5 ? 1.0 : materialAlpha);
  float textureAlpha = textureHasAlpha > 0.5 ? texel.a : 1.0;
  vec3 rgb = visualColor.rgb;
  float alpha = primaryAlpha;

  switch (textureModel) {
  case COIN_TEXTURE_MODEL_DECAL:
    rgb = mix(visualColor.rgb, texel.rgb, textureAlpha);
    break;
  case COIN_TEXTURE_MODEL_BLEND:
    rgb = mix(visualColor.rgb, blendColor.rgb, texel.rgb);
    alpha = primaryAlpha * textureAlpha;
    break;
  case COIN_TEXTURE_MODEL_REPLACE:
    rgb = texel.rgb;
    alpha = textureHasAlpha > 0.5 ? texel.a : primaryAlpha;
    break;
  case COIN_TEXTURE_MODEL_MODULATE:
  default:
    rgb = visualColor.rgb * texel.rgb;
    alpha = primaryAlpha * textureAlpha;
    break;
  }
  return vec4(rgb, alpha);
}
