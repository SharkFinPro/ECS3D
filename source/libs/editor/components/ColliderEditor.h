#ifndef COLLIDEREDITOR_H
#define COLLIDEREDITOR_H

#include <functional>
#include <memory>

class ComponentEditor;
class Terrain;
class GpuAssetCache;
class AssetRegistry;
namespace terrain { class TerrainStore; }

// The asset cache + registry let the convex mesh collider's source model slot show the assigned model and
// rebuild the hull from it. Both may be null, in which case the slot shows no name and cannot rebuild. The Terrain
// section's read-only progress rows use the cache and the terrain store; either may be null. drawSculpt, when set,
// draws the sculpt controls inside the Terrain section for the component being shown.
void registerColliderEditors(ComponentEditor& componentEditor,
                             std::shared_ptr<GpuAssetCache> assetCache = nullptr,
                             const AssetRegistry* assetRegistry = nullptr,
                             const terrain::TerrainStore* terrainStore = nullptr,
                             std::function<void(const std::shared_ptr<Terrain>&)> drawSculpt = {});



#endif //COLLIDEREDITOR_H
