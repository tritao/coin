#ifndef COIN_INTERNAL_GL_READBACK_H
#define COIN_INTERNAL_GL_READBACK_H

#ifndef COIN_INTERNAL
#error this is a private header file
#endif /* ! COIN_INTERNAL */

#include <cstdint>

#include <Inventor/SbVec2s.h>

// Shared readback implementation used by CoinOffscreenGLCanvas and the
// profile-neutral OpenGL tests.  The legacyContext flag is only used for the
// compatibility-only pixel-transfer state; pack state is restored for every
// context profile.
void coin_read_pixels(uint8_t * dst, const SbVec2s & vpdims,
                      unsigned int dstrowsize, unsigned int nrcomponents,
                      SbBool legacyContext);

#endif // COIN_INTERNAL_GL_READBACK_H
