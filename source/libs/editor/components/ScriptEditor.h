#ifndef SCRIPTEDITOR_H
#define SCRIPTEDITOR_H

#include <nlohmann/json_fwd.hpp>

class ComponentEditor;
class MixedFields;

// Draws one widget per exposed field and writes an edited value back into `fields`. Returns whether any
// field changed this frame. Shared by the object Script handler and the Scene Scripts panel.
bool drawScriptFieldWidgets(nlohmann::json& fields, const MixedFields& mixed);

void registerScriptEditor(ComponentEditor& componentEditor);



#endif //SCRIPTEDITOR_H
