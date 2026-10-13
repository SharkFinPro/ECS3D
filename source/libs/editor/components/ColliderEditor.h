#ifndef COLLIDEREDITOR_H
#define COLLIDEREDITOR_H

#include <memory>

class ComponentEditor;
class GpuAssetCache;
class AssetRegistry;

// The asset cache + registry let the convex mesh collider's source model slot show the assigned model and
// rebuild the hull from it. Both may be null, in which case the slot shows no name and cannot rebuild.
void registerColliderEditors(ComponentEditor& componentEditor,
                             std::shared_ptr<GpuAssetCache> assetCache = nullptr,
                             const AssetRegistry* assetRegistry = nullptr);



#endif //COLLIDEREDITOR_H
