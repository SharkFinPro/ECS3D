#include "ScriptEditor.h"
#include "../ComponentEditor.h"
#include <memory>

class Component;

void registerScriptEditor(ComponentEditor& componentEditor)
{
  componentEditor.registerHandler("Script", [](const std::shared_ptr<Component>& component) {
    // TODO: migrate Script::displayGui / displayFieldsGui here. Cast to Script (ECS3DData), draw the
    // TODO:   header with the className, then an input per exposed field. The field values live in the
    // TODO:   C# instance on the server, so the editor reads/writes them over the network (request the
    // TODO:   exposed-field list + values, send edits as commands) rather than calling a local ScriptManager.
    (void)component;
  });
}
