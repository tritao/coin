// src/rendering/SoTextureQualityPolicy.cpp

#include "rendering/SoTextureQualityPolicy.h"

#include <Inventor/C/tidbits.h>

#include <cstdlib>

namespace {

float
texture_quality_limit(const char * name, const float fallback)
{
  const char * env = coin_getenv(name);
  if (!env) return fallback;

  const float value = static_cast<float>(std::atof(env));
  return value < 0.0f || value > 1.0f ? fallback : value;
}

}

const SoTextureQualityPolicy &
coin_texture_quality_policy()
{
  static const SoTextureQualityPolicy policy = {
    texture_quality_limit("COIN_TEX2_LINEAR_LIMIT", 0.2f),
    texture_quality_limit("COIN_TEX2_MIPMAP_LIMIT", 0.5f),
    texture_quality_limit("COIN_TEX2_LINEAR_MIPMAP_LIMIT", 0.8f)
  };
  return policy;
}

SoTextureFilter
coin_texture_min_filter_for_quality(const float quality)
{
  const SoTextureQualityPolicy & policy = coin_texture_quality_policy();
  if (quality < policy.linearLimit) {
    return SO_TEXTURE_FILTER_NEAREST;
  }
  if (quality < policy.mipmapLimit) {
    return SO_TEXTURE_FILTER_LINEAR;
  }
  if (quality < policy.linearMipmapLimit) {
    return SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR;
  }
  return SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR;
}

SoTextureFilter
coin_texture_mag_filter_for_quality(const float quality)
{
  const SoTextureQualityPolicy & policy = coin_texture_quality_policy();
  return quality < policy.linearLimit
    ? SO_TEXTURE_FILTER_NEAREST
    : SO_TEXTURE_FILTER_LINEAR;
}

bool
coin_texture_filter_uses_mipmap(const SoTextureFilter filter)
{
  return filter == SO_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
         filter == SO_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST ||
         filter == SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR ||
         filter == SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR;
}

SoTextureFilter
coin_texture_filter_without_mipmaps(const SoTextureFilter filter)
{
  switch (filter) {
  case SO_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
  case SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
    return SO_TEXTURE_FILTER_NEAREST;
  case SO_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
  case SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
    return SO_TEXTURE_FILTER_LINEAR;
  default:
    return filter;
  }
}
