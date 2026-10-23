#include "ScriptEditor.h"
#include "../ComponentEditor.h"
#include "../AssetDisplay.h"
#include "../AssetDragDrop.h"
#include "../EditorTheme.h"
#include "../GuiComponents.h"
#include "../MixedFields.h"
#include "AssetSlot.h"
#include "ScriptFieldAsset.h"
#include "ScriptFieldLabel.h"
#include <objects/components/Script.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace {
  int growStringBuffer(ImGuiInputTextCallbackData* data)
  {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
      auto* text = static_cast<std::string*>(data->UserData);
      text->resize(static_cast<std::size_t>(data->BufTextLen));
      data->Buf = text->data();
    }

    return 0;
  }

  bool inputTextString(const char* label, const char* hint, std::string& text)
  {
    return ImGui::InputTextWithHint(label, hint, text.data(), text.capacity() + 1,
                                    ImGuiInputTextFlags_CallbackResize, growStringBuffer, &text);
  }
}

bool drawScriptFieldWidgets(nlohmann::json& fields, const MixedFields& mixed, const AssetRegistry* registry,
                            const std::shared_ptr<GpuAssetCache>& cache)
{
  bool edited = false;

  for (auto& field : fields)
  {
    if (!field.contains("name") || !field.contains("type") || !field.contains("value"))
    {
      continue;
    }

    const std::string name = field.at("name");
    const std::string type = field.at("type");
    const std::string label = scriptFieldLabel(field) + "##" + name;
    // mixedTopLevelKeys reports a disagreeing script field as "fields.<name>" rather than the whole
    // "fields" array, so only this one entry's widget shows a mixed state.
    const bool fieldMixed = mixed.contains("fields." + name);

    ImGui::PushID(name.c_str());

    if (type == "float")
    {
      float value = field.at("value");
      const float previous = value;
      ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, fieldMixed);
      const bool changed = ImGui::DragFloat(label.c_str(), &value, 0.1f);
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
      const bool changed = ImGui::DragInt(label.c_str(), &value);
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
      const bool changed = ImGui::Checkbox(label.c_str(), &value);
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
    else if (const auto assetType = scriptFieldAssetType(field))
    {
      const std::string kind = scriptFieldLabel(field);
      const AssetSlotStyle slot{ "assetSlot", kind.c_str(), assetDragDrop::payloadId(*assetType),
                                 *assetType == AssetType::Texture, assetDisplay::icon(*assetType),
                                 assetDisplay::color(*assetType) };
      uuids::uuid dropped;

      if (drawAssetSlot(cache, registry, slot, scriptFieldUUID(field.at("value")), dropped, fieldMixed))
      {
        field["value"] = uuids::to_string(dropped);
        edited = true;
      }

      if (ImGui::BeginPopupContextItem("assetSlotMenu"))
      {
        const bool empty = field.at("value").is_string() && field.at("value").get<std::string>().empty();
        if (ImGui::MenuItem("Clear", nullptr, false, fieldMixed || !empty))
        {
          field["value"] = "";
          edited = true;
        }

        ImGui::EndPopup();
      }
    }
    else if (type == "string")
    {
      const auto& value = field.at("value");
      // A mixed field shows empty with a hint, but not while it is being typed into: the typed text
      // is the primary's value then, and blanking it would drop every keystroke after the first.
      const bool typing = ImGui::GetActiveID() == ImGui::GetID(label.c_str());
      std::string text = value.is_string() && (!fieldMixed || typing) ? value.get<std::string>() : std::string();

      if (inputTextString(label.c_str(), fieldMixed ? "(mixed)" : "", text))
      {
        field["value"] = std::move(text);
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
      const bool changed = ImGui::DragFloat3(label.c_str(), vec, 0.1f);
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
      ImGui::LabelText(label.c_str(), "%s", field.at("value").is_string()
        ? std::string(field.at("value")).c_str() : "");
    }

    ImGui::PopID();
  }

  return edited;
}

void registerScriptEditor(ComponentEditor& componentEditor, const AssetRegistry* registry,
                          const std::shared_ptr<GpuAssetCache>& cache)
{
  componentEditor.registerHandler("Script",
    [registry, cache](const std::shared_ptr<Component>& component, const MixedFields& mixed) -> bool {
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
      edited = drawScriptFieldWidgets(fields, mixed, registry, cache);

      if (edited)
      {
        script->setFields(fields);
      }
    }

    return edited;
  });
}
