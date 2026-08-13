// src/rendering/SoTextureQualityPolicy.h

#ifndef COIN_SOTEXTUREQUALITYPOLICY_H
#define COIN_SOTEXTUREQUALITYPOLICY_H

#include <Inventor/rendering/SoRenderIR.h>

struct SoTextureQualityPolicy {
  float linearLimit;
  float mipmapLimit;
  float linearMipmapLimit;
};

COIN_DLL_API const SoTextureQualityPolicy & coin_texture_quality_policy();
COIN_DLL_API SoTextureFilter coin_texture_min_filter_for_quality(float quality);
COIN_DLL_API SoTextureFilter coin_texture_mag_filter_for_quality(float quality);
COIN_DLL_API bool coin_texture_filter_uses_mipmap(SoTextureFilter filter);
COIN_DLL_API SoTextureFilter coin_texture_filter_without_mipmaps(SoTextureFilter filter);

#endif // COIN_SOTEXTUREQUALITYPOLICY_H
