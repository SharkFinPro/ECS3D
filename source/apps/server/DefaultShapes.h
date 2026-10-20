#ifndef DEFAULTSHAPES_H
#define DEFAULTSHAPES_H

#include <objects/components/collisions/ConvexHull.h>
#include <nlohmann/json_fwd.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

// Convex hulls for the built-in prefabs, built from the same formulas as the models in
// tools/shapes through buildConvexHull. Each throws std::runtime_error if the builder refuses its
// own input, which would be a bug here rather than a runtime condition.

// The ramp.glb wedge: six corners on the -1..1 footprint, rising from y = -1 at x = -1 to y = 1 at x = 1.
[[nodiscard]] ConvexHull rampHull();

// The cylinder.glb rings: radius 1 at y = -1 and y = 1, 32 vertices each.
[[nodiscard]] ConvexHull cylinderHull();

// The convex shape library. Vertices follow the formulas in tools/shapes/library.py exactly, so each hull is
// the one its model draws. d6Hull is the cube of cube_1x1x1.glb.
[[nodiscard]] ConvexHull d4Hull();
[[nodiscard]] ConvexHull d6Hull();
[[nodiscard]] ConvexHull d8Hull();
[[nodiscard]] ConvexHull d12Hull();
[[nodiscard]] ConvexHull d20Hull();

// Radius 0.6 and height 2 along Y, six-sided.
[[nodiscard]] ConvexHull hexPrismHull();

// Radius 1 and height 0.9 along Y, sixteen-sided.
[[nodiscard]] ConvexHull drumHull();

// A point below, an eight-sided girdle and an eight-sided table above.
[[nodiscard]] ConvexHull gemHull();

// The Quickhull of boulderPoints.
[[nodiscard]] ConvexHull boulderHull();

// The 24 seeded points the boulder is hulled from, before interior ones are dropped.
[[nodiscard]] std::vector<glm::vec3> boulderPoints();

// Volume of the hull by the same code the inertia tensor uses, so mass at density one matches physics.
[[nodiscard]] float hullVolume(const ConvexHull& hull);

// A serialized ConvexMesh collider at the identity offset holding hull and recording sourceModel.
[[nodiscard]] nlohmann::json convexMeshCollider(const ConvexHull& hull, const std::string& sourceModel);

#endif //DEFAULTSHAPES_H
