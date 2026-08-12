// src/rendering/SoTextureQualityPolicy.h

#ifndef COIN_SOTEXTUREQUALITYPOLICY_H
#define COIN_SOTEXTUREQUALITYPOLICY_H

#include "tidbitsp.h"

#include <cstdlib>

// This is the shared semantic quality decision used by LegacyGL and retained
// capture. API-specific code maps the result to its own sampler state.
struct CoinTextureQualityLimits {
  float linear = 0.2f;
  float mipmap = 0.5f;
  float linearMipmap = 0.8f;
  float anisotropic = 0.85f;
};

inline float
coin_texture_quality_limit(const char * name, const float fallback)
{
  const char * value = coin_getenv(name);
  if (!value) return fallback;
  const float parsed = static_cast<float>(std::atof(value));
  return parsed >= 0.0f && parsed <= 1.0f ? parsed : fallback;
}

inline float
coin_texture_anisotropic_limit()
{
  const char * value = coin_getenv("COIN_TEX2_ANISOTROPIC_LIMIT");
  return value ? static_cast<float>(std::atof(value)) : 0.85f;
}

inline CoinTextureQualityLimits
coin_read_texture_quality_limits()
{
  CoinTextureQualityLimits limits;
  limits.linear = coin_texture_quality_limit(
    "COIN_TEX2_LINEAR_LIMIT", limits.linear);
  limits.mipmap = coin_texture_quality_limit(
    "COIN_TEX2_MIPMAP_LIMIT", limits.mipmap);
  limits.linearMipmap = coin_texture_quality_limit(
    "COIN_TEX2_LINEAR_MIPMAP_LIMIT", limits.linearMipmap);
  // LegacyGL intentionally accepts the anisotropic threshold verbatim. In
  // particular, it does not clamp out-of-range environment values the way
  // the ordinary quality thresholds are validated.
  limits.anisotropic = coin_texture_anisotropic_limit();
  return limits;
}

inline const CoinTextureQualityLimits &
coin_get_texture_quality_limits()
{
  // SoGLImage caches these values on first use. Keep retained capture on the
  // same one-time snapshot instead of rereading the environment for every
  // command.
  static const CoinTextureQualityLimits limits =
    coin_read_texture_quality_limits();
  return limits;
}

struct CoinTextureQualityPolicy {
  bool linear = false;
  bool mipmap = false;
  bool linearMipmap = false;
  bool anisotropic = false;
};

inline CoinTextureQualityPolicy
coin_get_texture_quality_policy(const float quality)
{
  const CoinTextureQualityLimits & limits = coin_get_texture_quality_limits();
  CoinTextureQualityPolicy policy;
  policy.linear = quality >= limits.linear;
  policy.mipmap = quality >= limits.mipmap;
  policy.linearMipmap = quality >= limits.linearMipmap;
  policy.anisotropic = quality > limits.anisotropic;
  return policy;
}

#endif // COIN_SOTEXTUREQUALITYPOLICY_H
