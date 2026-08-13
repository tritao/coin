/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
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

#include "shapenodes/soshape_trianglesort.h"
#include "rendering/SoTransparencySortP.h"

#include <cassert>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif // HAVE_CONFIG_H

#include <Inventor/lists/SbList.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/bundles/SoMaterialBundle.h>
#include <Inventor/system/gl.h>

soshape_trianglesort::soshape_trianglesort(void)
{
  this->pvlist = NULL;
}

soshape_trianglesort::~soshape_trianglesort()
{
  delete this->pvlist;
}

void
soshape_trianglesort::beginShape(SoState *)
{
  if (this->pvlist == NULL) {
    this->pvlist = new SbList <SoPrimitiveVertex>;
  }
  pvlist->truncate(0);
}

void
soshape_trianglesort::triangle(SoState *,
                          const SoPrimitiveVertex * v1,
                          const SoPrimitiveVertex * v2,
                          const SoPrimitiveVertex * v3)
{
  assert(this->pvlist);
  this->pvlist->append(*v1);
  this->pvlist->append(*v2);
  this->pvlist->append(*v3);
}

void
soshape_trianglesort::endShape(SoState * state, SoMaterialBundle & mb)
{
  const int n = this->pvlist->getLength() / 3;
  if (n == 0) return;

  const SoPrimitiveVertex * varray = this->pvlist->getArrayPtr();

  std::vector<SbVec3f> points;
  points.reserve(static_cast<size_t>(n) * 3);
  for (int i = 0; i < n * 3; ++i) points.push_back(varray[i].getPoint());

  std::vector<int> order;
  SoTransparencySortTriangles(state, points.data(), n, order);

  // this rendering loop can be optimized a lot, of course, but speed
  // is not so important here, since it's slow to generate, copy and
  // sort the triangles anyway.
  glBegin(GL_TRIANGLES);
  for (int i = 0; i < n; i++) {
    const int idx = order[static_cast<size_t>(i)] * 3;
    const SoPrimitiveVertex * v = varray + idx;
    glTexCoord4fv(v->getTextureCoords().getValue());
    glNormal3fv(v->getNormal().getValue());
    mb.send(v->getMaterialIndex(), TRUE);
    glVertex3fv(v->getPoint().getValue());

    v = varray + idx+1;
    glTexCoord4fv(v->getTextureCoords().getValue());
    glNormal3fv(v->getNormal().getValue());
    mb.send(v->getMaterialIndex(), TRUE);
    glVertex3fv(v->getPoint().getValue());

    v = varray + idx+2;
    glTexCoord4fv(v->getTextureCoords().getValue());
    glNormal3fv(v->getNormal().getValue());
    mb.send(v->getMaterialIndex(), TRUE);
    glVertex3fv(v->getPoint().getValue());
  }
  glEnd();
}
