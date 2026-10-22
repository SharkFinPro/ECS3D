#ifndef SCRIPTEDITOR_H
#define SCRIPTEDITOR_H

#include <nlohmann/json_fwd.hpp>

class AssetRegistry;
class ComponentEditor;
class MixedFields;

// Draws one widget per exposed field and writes an edited value back into `fields`. Returns whether any
// field changed this frame. registry names the assets a prefab slot shows and may be null. Shared by the
// object Script handler and the Scene Scripts panel.
bool drawScriptFieldWidgets(nlohmann::json& fields, const MixedFields& mixed, const AssetRegistry* registry);

void registerScriptEditor(ComponentEditor& componentEditor, const AssetRegistry* registry);

#endif //SCRIPTEDITOR_H
