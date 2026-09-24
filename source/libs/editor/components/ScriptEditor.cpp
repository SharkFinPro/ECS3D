#include "ScriptEditor.h"
#include "../ComponentEditor.h"
#include "../GuiComponents.h"
#include "../MixedFields.h"
#include <objects/components/Script.h>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <memory>
#include <string>

void registerScriptEditor(ComponentEditor& componentEditor)
{
  componentEditor.registerHandler("Script",
    [](const std::shared_ptr<Component>& component, const MixedFields& mixed) -> bool {
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
        // mixedTopLevelKeys reports a disagreeing script field as "fields.<name>" rather than the whole
        // "fields" array, so only this one entry's widget shows a mixed state.
        const bool fieldMixed = mixed.contains("fields." + name);

        ImGui::PushID(name.c_str());

        if (type == "float")
        {
          float value = field.at("value");
          const float previous = value;
          ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, fieldMixed);
          const bool changed = ImGui::DragFloat(name.c_str(), &value, 0.1f);
          ImGui::PopItemFlag();
          if (changed && gc::acceptFinite(name.c_str(), &value, previous))
          {
            field["value"] = value;
            edited = true;
          }
        }
        else if (type == "int")
        {
          int value = field.at("value");
          ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, fieldMixed);
          const bool changed = ImGui::DragInt(name.c_str(), &value);
          ImGui::PopItemFlag();
          if (changed)
          {
            field["value"] = value;
            edited = true;
          }
        }
        else if (type == "bool")
        {
          bool value = field.at("value");
          ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, fieldMixed);
          const bool changed = ImGui::Checkbox(name.c_str(), &value);
          ImGui::PopItemFlag();
          if (changed)
          {
            // ImGui's MixedValue only changes the rendering (the dash) - a click still flips whichever
            // value the primary object happened to hold, which can land on false. A click on a mixed
            // checkbox resolves the whole selection to true, matching accentCheckbox's own rule.
            field["value"] = fieldMixed ? true : value;
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
          const float previous[3] = { vec[0], vec[1], vec[2] };
          ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, fieldMixed);
          const bool changed = ImGui::DragFloat3(name.c_str(), vec, 0.1f);
          ImGui::PopItemFlag();
          if (changed)
          {
            bool finite = true;
            for (int i = 0; i < 3; ++i)
            {
              finite = gc::acceptFinite(name.c_str(), &vec[i], previous[i]) && finite;
            }

            if (finite)
            {
              field["value"] = { vec[0], vec[1], vec[2] };
              edited = true;
            }
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
