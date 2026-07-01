#ifndef MODELRENDEREREDITOR_H
#define MODELRENDEREREDITOR_H

#include <memory>

class ComponentEditor;
class GpuAssetCache;
class AssetRegistry;

// The asset cache + registry let the renderer's Model/Texture/Specular slots show the same thumbnails
// as the asset grid (matching the mockup). Both may be null, in which case the slots show type icons.
void registerModelRendererEditor(ComponentEditor& componentEditor,
                                 std::shared_ptr<GpuAssetCache> assetCache = nullptr,
                                 const AssetRegistry* assetRegistry = nullptr);



#endif //MODELRENDEREREDITOR_H
