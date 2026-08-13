#include "rendering/SoTextureQualityPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void set_environment(const char * name, const char * value)
{
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

void clear_environment(const char * name)
{
#ifdef _WIN32
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}

bool expect(bool condition, const char * message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
  }
  return condition;
}

bool closeEnough(float actual, float expected)
{
  return std::abs(actual - expected) < 0.0001f;
}

void clear_policy_environment()
{
  clear_environment("COIN_TEX2_LINEAR_LIMIT");
  clear_environment("COIN_TEX2_MIPMAP_LIMIT");
  clear_environment("COIN_TEX2_LINEAR_MIPMAP_LIMIT");
}

int run(const std::string & mode)
{
  clear_policy_environment();
  if (mode == "override") {
    set_environment("COIN_TEX2_LINEAR_LIMIT", "0.4");
    set_environment("COIN_TEX2_MIPMAP_LIMIT", "0.7");
    set_environment("COIN_TEX2_LINEAR_MIPMAP_LIMIT", "0.9");
  }
  else if (mode == "invalid") {
    set_environment("COIN_TEX2_LINEAR_LIMIT", "-1.0");
    set_environment("COIN_TEX2_MIPMAP_LIMIT", "1.1");
    set_environment("COIN_TEX2_LINEAR_MIPMAP_LIMIT", "-0.1");
  }
  else if (mode != "default") {
    std::cerr << "usage: TextureQualityPolicyTest [default|override|invalid]" << std::endl;
    return 2;
  }

  const SoTextureQualityPolicy & policy = coin_texture_quality_policy();
  const float linearLimit = mode == "override" ? 0.4f : 0.2f;
  const float mipmapLimit = mode == "override" ? 0.7f : 0.5f;
  const float linearMipmapLimit = mode == "override" ? 0.9f : 0.8f;
  int result = 0;
  result |= !expect(closeEnough(policy.linearLimit, linearLimit), "wrong linear threshold");
  result |= !expect(closeEnough(policy.mipmapLimit, mipmapLimit), "wrong mipmap threshold");
  result |= !expect(closeEnough(policy.linearMipmapLimit, linearMipmapLimit),
                    "wrong trilinear threshold");

  result |= !expect(coin_texture_min_filter_for_quality(0.1f) == SO_TEXTURE_FILTER_NEAREST,
                    "low quality must use nearest filtering");
  result |= !expect(coin_texture_min_filter_for_quality(
                      (linearLimit + mipmapLimit) * 0.5f) == SO_TEXTURE_FILTER_LINEAR,
                    "medium quality must use linear filtering");
  result |= !expect(coin_texture_min_filter_for_quality(
                      (mipmapLimit + linearMipmapLimit) * 0.5f)
                      == SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR,
                    "high quality must use trilinear filtering");
  result |= !expect(coin_texture_min_filter_for_quality(0.95f)
                      == SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR,
                    "maximum quality must use linear mipmap filtering");
  result |= !expect(coin_texture_mag_filter_for_quality(0.1f) == SO_TEXTURE_FILTER_NEAREST,
                    "low quality magnification must use nearest filtering");
  result |= !expect(coin_texture_mag_filter_for_quality(0.5f) == SO_TEXTURE_FILTER_LINEAR,
                    "normal magnification must use linear filtering");

  result |= !expect(coin_texture_filter_uses_mipmap(SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR),
                    "mipmap filter was not recognized");
  result |= !expect(!coin_texture_filter_uses_mipmap(SO_TEXTURE_FILTER_LINEAR),
                    "base-level filter was recognized as a mipmap filter");
  result |= !expect(coin_texture_filter_without_mipmaps(SO_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR)
                      == SO_TEXTURE_FILTER_NEAREST,
                    "nearest mipmap fallback is wrong");
  result |= !expect(coin_texture_filter_without_mipmaps(SO_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR)
                      == SO_TEXTURE_FILTER_LINEAR,
                    "linear mipmap fallback is wrong");
  return result;
}

}

int main(int argc, char ** argv)
{
  return run(argc > 1 ? argv[1] : "default");
}
