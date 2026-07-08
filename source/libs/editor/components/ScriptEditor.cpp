#include "ScriptEditor.h"
#include "../ComponentEditor.h"
#include <objects/components/Script.h>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <memory>
#include <string>

void registerScriptEditor(ComponentEditor& componentEditor)
{
  componentEditor.registerHandler("Script", [](const std::shared_ptr<Component>& component) -> bool {
    const auto script = std::dynamic_pointer_cast<Script>(component);
    if (!script)
    {
      return false;
    }

    bool edited = false;

    if (ComponentEditor::displayHeader(component, "Script (" + script->getClassName() + ")"))
    {
      // The exposed-field blob is synced from the live C# instance on the server. Edit a copy and push
      // it back; the editor sends it on as a component edit, the server writes it into the instance.
      nlohmann::json fields = script->getFields();

      for (auto& field : fields)
      {
        if (!field.contains("name") || !field.contains("type") || !field.contains("value"))
        {
          continue;
        }

        const std::string name = field.at("name");
        const std::string type = field.at("type");

        ImGui::PushID(name.c_str());

        if (type == "float")
        {
          float value = field.at("value");
          if (ImGui::DragFloat(name.c_str(), &value, 0.1f))
          {
            field["value"] = value;
            edited = true;
          }
        }
        else if (type == "int")
        {
          int value = field.at("value");
          if (ImGui::DragInt(name.c_str(), &value))
          {
            field["value"] = value;
            edited = true;
          }
        }
        else if (type == "bool")
        {
          bool value = field.at("value");
          if (ImGui::Checkbox(name.c_str(), &value))
          {
            field["value"] = value;
            edited = true;
          }
        }
        else if (type == "vector3")
        {
          const auto& value = field.at("value");
          float vec[3] = {
            value.is_array() && value.size() == 3 ? value.at(0).get<float>() : 0.0f,
            value.is_array() && value.size() == 3 ? value.at(1).get<float>() : 0.0f,
            value.is_array() && value.size() == 3 ? value.at(2).get<float>() : 0.0f
          };
          if (ImGui::DragFloat3(name.c_str(), vec, 0.1f))
          {
            field["value"] = { vec[0], vec[1], vec[2] };
            edited = true;
          }
        }
        else
        {
          ImGui::LabelText(name.c_str(), "%s", field.at("value").is_string()
            ? std::string(field.at("value")).c_str() : "");
        }

        ImGui::PopID();
      }

      if (edited)
      {
        script->setFields(fields);
      }
    }

    return edited;
  });
}
