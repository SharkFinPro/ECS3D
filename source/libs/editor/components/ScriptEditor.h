#ifndef SCRIPTEDITOR_H
#define SCRIPTEDITOR_H

#include <nlohmann/json_fwd.hpp>
#include <memory>

class AssetRegistry;
class ComponentEditor;
class GpuAssetCache;
class MixedFields;

// Draws one widget per exposed field and writes an edited value back into `fields`. Returns whether any
// field changed this frame. registry names the assets an asset slot shows and cache draws a texture slot's
// thumbnail; both may be null. Shared by the object Script handler and the Scene Scripts panel.
bool drawScriptFieldWidgets(nlohmann::json& fields, const MixedFields& mixed, const AssetRegistry* registry,
                            const std::shared_ptr<GpuAssetCache>& cache);

void registerScriptEditor(ComponentEditor& componentEditor, const AssetRegistry* registry,
                          const std::shared_ptr<GpuAssetCache>& cache);

#endif //SCRIPTEDITOR_H
