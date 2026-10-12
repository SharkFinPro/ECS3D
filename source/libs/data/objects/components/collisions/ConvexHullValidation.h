#ifndef CONVEXHULLVALIDATION_H
#define CONVEXHULLVALIDATION_H

#include "ConvexHull.h"
#include <cstddef>

// A hull of V vertices has at most 2V - 4 triangles.
constexpr size_t maxConvexHullIndices = 3 * (2 * maxConvexHullVertices - 4);

// True when the hull has 4..maxConvexHullVertices finite vertices and a non-empty triangle list within
// maxConvexHullIndices whose indices all name a vertex. It checks structure, not convexity.
[[nodiscard]] bool isValidConvexHull(const ConvexHull& hull);

// The axis-aligned cube with corners at +/-1.
[[nodiscard]] ConvexHull makeUnitCubeHull();

#endif //CONVEXHULLVALIDATION_H
