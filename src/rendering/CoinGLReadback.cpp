/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\**************************************************************************/

#include "CoinGLReadback.h"

#include <Inventor/system/gl.h>

#include <cassert>

void
coin_read_pixels(uint8_t * dst, const SbVec2s & vpdims,
                 unsigned int dstrowsize, unsigned int nrcomponents,
                 const SbBool legacyContext)
{
#if COIN_BUILD_LEGACY_GL_RENDERER
  if (legacyContext) {
    glPixelTransferi(GL_MAP_COLOR, 0);
    glPixelTransferi(GL_MAP_STENCIL, 0);
    glPixelTransferi(GL_INDEX_SHIFT, 0);
    glPixelTransferi(GL_INDEX_OFFSET, 0);
    glPixelTransferf(GL_RED_SCALE, 1);
    glPixelTransferf(GL_RED_BIAS, 0);
    glPixelTransferf(GL_GREEN_SCALE, 1);
    glPixelTransferf(GL_GREEN_BIAS, 0);
    glPixelTransferf(GL_BLUE_SCALE, 1);
    glPixelTransferf(GL_BLUE_BIAS, 0);
    glPixelTransferf(GL_ALPHA_SCALE, 1);
    glPixelTransferf(GL_ALPHA_BIAS, 0);
    glPixelTransferf(GL_DEPTH_SCALE, 1);
    glPixelTransferf(GL_DEPTH_BIAS, 0);

    GLuint i = 0;
    GLfloat f = 0.0f;
    glPixelMapfv(GL_PIXEL_MAP_I_TO_I, 1, &f);
    glPixelMapuiv(GL_PIXEL_MAP_S_TO_S, 1, &i);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_R, 1, &f);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_G, 1, &f);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_B, 1, &f);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_A, 1, &f);
    glPixelMapfv(GL_PIXEL_MAP_R_TO_R, 1, &f);
    glPixelMapfv(GL_PIXEL_MAP_G_TO_G, 1, &f);
    glPixelMapfv(GL_PIXEL_MAP_B_TO_B, 1, &f);
    glPixelMapfv(GL_PIXEL_MAP_A_TO_A, 1, &f);
  }
#endif

#if COIN_BUILD_LEGACY_GL_RENDERER
  GLint packSwapBytes = 0;
  GLint packLsbFirst = 0;
  if (legacyContext) {
    glGetIntegerv(GL_PACK_SWAP_BYTES, &packSwapBytes);
    glGetIntegerv(GL_PACK_LSB_FIRST, &packLsbFirst);
  }
#endif
  GLint packRowLength;
  GLint packSkipRows;
  GLint packSkipPixels;
  GLint packAlignment;
  GLint packImageHeight;
  GLint packSkipImages;
  glGetIntegerv(GL_PACK_ROW_LENGTH, &packRowLength);
  glGetIntegerv(GL_PACK_SKIP_ROWS, &packSkipRows);
  glGetIntegerv(GL_PACK_SKIP_PIXELS, &packSkipPixels);
  glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
  glGetIntegerv(GL_PACK_IMAGE_HEIGHT, &packImageHeight);
  glGetIntegerv(GL_PACK_SKIP_IMAGES, &packSkipImages);

#if COIN_BUILD_LEGACY_GL_RENDERER
  if (legacyContext) {
    glPixelStorei(GL_PACK_SWAP_BYTES, 0);
    glPixelStorei(GL_PACK_LSB_FIRST, 0);
  }
#endif
  glPixelStorei(GL_PACK_ROW_LENGTH, (GLint) dstrowsize);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
  glPixelStorei(GL_PACK_SKIP_IMAGES, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);

  glFlush();
  glFinish();

  assert((nrcomponents >= 1) && (nrcomponents <= 4));
  if (nrcomponents < 3) {
    unsigned char * tmp = new unsigned char[vpdims[0] * vpdims[1] * 4];
    glReadPixels(0, 0, vpdims[0], vpdims[1],
                 nrcomponents == 1 ? GL_RGB : GL_RGBA,
                 GL_UNSIGNED_BYTE, tmp);

    const unsigned char * src = tmp;
    for (short y = 0; y < vpdims[1]; y++) {
      for (short x = 0; x < vpdims[0]; x++) {
        const double value = src[0] * 0.3 + src[1] * 0.59 + src[2] * 0.11;
        *dst++ = (unsigned char) value;
        if (nrcomponents == 2) *dst++ = src[3];
        src += nrcomponents == 1 ? 3 : 4;
      }
    }
    delete[] tmp;
  }
  else {
    glReadPixels(0, 0, vpdims[0], vpdims[1],
                 nrcomponents == 3 ? GL_RGB : GL_RGBA,
                 GL_UNSIGNED_BYTE, dst);
  }
  glFlush();
  glFinish();

#if COIN_BUILD_LEGACY_GL_RENDERER
  if (legacyContext) {
    glPixelStorei(GL_PACK_SWAP_BYTES, packSwapBytes);
    glPixelStorei(GL_PACK_LSB_FIRST, packLsbFirst);
  }
#endif
  glPixelStorei(GL_PACK_ROW_LENGTH, packRowLength);
  glPixelStorei(GL_PACK_SKIP_ROWS, packSkipRows);
  glPixelStorei(GL_PACK_SKIP_PIXELS, packSkipPixels);
  glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
  glPixelStorei(GL_PACK_IMAGE_HEIGHT, packImageHeight);
  glPixelStorei(GL_PACK_SKIP_IMAGES, packSkipImages);
}
