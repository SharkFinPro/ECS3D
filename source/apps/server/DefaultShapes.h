#ifndef DEFAULTSHAPES_H
#define DEFAULTSHAPES_H

#include <objects/components/collisions/ConvexHull.h>
#include <nlohmann/json_fwd.hpp>
#include <string>

// Convex hulls for the built-in prefabs, built from the same formulas as the models in
// tools/shapes through buildConvexHull. Each throws std::runtime_error if the builder refuses its
// own input, which would be a bug here rather than a runtime condition.

// The ramp.glb wedge: six corners on the -1..1 footprint, rising from y = -1 at x = -1 to y = 1 at x = 1.
[[nodiscard]] ConvexHull rampHull();

// The cylinder.glb rings: radius 1 at y = -1 and y = 1, 32 vertices each.
[[nodiscard]] ConvexHull cylinderHull();

// Volume of the hull by the same code the inertia tensor uses, so mass at density one matches physics.
[[nodiscard]] float hullVolume(const ConvexHull& hull);

// A serialized ConvexMesh collider at the identity offset holding hull and recording sourceModel.
[[nodiscard]] nlohmann::json convexMeshCollider(const ConvexHull& hull, const std::string& sourceModel);

#endif //DEFAULTSHAPES_H
