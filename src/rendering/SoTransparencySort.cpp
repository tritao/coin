/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
 * All rights reserved.
 *
 * This file is part of Coin.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE,
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\**************************************************************************/

#include "rendering/SoTransparencySortP.h"

#include <Inventor/SbPlane.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoShapeHintsElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>

#include <algorithm>

namespace {

struct TriangleSortData {
  float distance;
  unsigned char backface;
  int index;
};

}

void
SoTransparencySortTriangles(SoState * state,
                            const SbVec3f * triangleVertices,
                            int triangleCount,
                            std::vector<int> & order)
{
  order.clear();
  if (!state || !triangleVertices || triangleCount <= 0) return;

  SoShapeHintsElement::VertexOrdering vertexOrdering;
  SoShapeHintsElement::ShapeType shapeType;
  SoShapeHintsElement::FaceType faceType;
  SoShapeHintsElement::get(state, vertexOrdering, shapeType, faceType);

  const bool useObjectDistance =
    vertexOrdering == SoShapeHintsElement::UNKNOWN_ORDERING ||
    (vertexOrdering != SoShapeHintsElement::UNKNOWN_ORDERING &&
     shapeType == SoShapeHintsElement::SOLID);

  const SbMatrix & modelMatrix = SoModelMatrixElement::get(state);
  SbPlane nearPlane;
  SbMatrix objectToViewport;
  const int clockwise = vertexOrdering == SoShapeHintsElement::CLOCKWISE ? 1 : 0;

  if (useObjectDistance) {
    nearPlane = SoViewVolumeElement::get(state).getPlane(0.0f);
    nearPlane = SbPlane(-nearPlane.getNormal(),
                        -nearPlane.getDistanceFromOrigin());
  }
  else {
    objectToViewport = modelMatrix;
    objectToViewport.multRight(SoViewingMatrixElement::get(state));
    objectToViewport.multRight(SoProjectionMatrixElement::get(state));
  }

  std::vector<TriangleSortData> triangles;
  triangles.reserve(static_cast<size_t>(triangleCount));
  for (int triangle = 0; triangle < triangleCount; ++triangle) {
    const SbVec3f * vertices = triangleVertices + triangle * 3;
    TriangleSortData data = {0.0f, 0, triangle};
    if (useObjectDistance) {
      SbVec3f center = vertices[0] + vertices[1] + vertices[2];
      center /= 3.0f;
      modelMatrix.multVecMatrix(center, center);
      data.distance = nearPlane.getDistance(center);
    }
    else {
      SbVec3f projected[3];
      float closest = 10.0f;
      for (int vertex = 0; vertex < 3; ++vertex) {
        projected[vertex] = vertices[vertex];
        objectToViewport.multVecMatrix(projected[vertex], projected[vertex]);
        closest = std::min(closest, projected[vertex][2]);
      }
      const SbVec3f edge0 = projected[2] - projected[0];
      const SbVec3f edge1 = projected[1] - projected[0];
      const float crossZ = edge0[0] * edge1[1] - edge0[1] * edge1[0];
      data.backface = static_cast<unsigned char>(
        crossZ < 0.0f ? 1 - clockwise : clockwise);
      data.distance = closest;
    }
    triangles.push_back(data);
  }

  std::stable_sort(triangles.begin(), triangles.end(),
    [](const TriangleSortData & lhs, const TriangleSortData & rhs) {
      if (lhs.distance != rhs.distance) return lhs.distance > rhs.distance;
      if (lhs.backface != rhs.backface) return lhs.backface > rhs.backface;
      return lhs.index < rhs.index;
    });

  order.reserve(static_cast<size_t>(triangleCount));
  for (const TriangleSortData & triangle : triangles) {
    order.push_back(triangle.index);
  }
}
