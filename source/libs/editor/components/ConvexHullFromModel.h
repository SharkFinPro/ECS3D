#ifndef CONVEXHULLFROMMODEL_H
#define CONVEXHULLFROMMODEL_H

#include <uuid.h>

class ConvexMeshCollider;
class GpuAssetCache;

// Builds the hull of the model's vertices (the engine loads only a model's first mesh) and stores it, with
// the model as the source, in the collider. Returns true when the collider changed. Any failure - no
// cache, an unknown or unloadable model, a degenerate shape - is logged as a warning and leaves the
// collider as it was. reportUnchanged makes a rebuild that changes nothing return true.
[[nodiscard]] bool rebuildHullFromModel(ConvexMeshCollider& collider, GpuAssetCache* assetCache,
                                        const uuids::uuid& modelUUID, bool reportUnchanged = false);

#endif //CONVEXHULLFROMMODEL_H
