#include "SceneScriptsPanel.h"
#include "AssetDragDrop.h"
#include "MixedFields.h"
#include "SceneScriptsPanelLogic.h"
#include "components/ScriptEditor.h"
#include <assets/AssetRegistry.h>
#include <scenes/SceneAsset.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneScriptEdits.h>
#include <imgui.h>
#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

namespace {
  constexpr const char* windowName = "Scene Scripts";
  constexpr const char* addPopupName = "##addSceneScript";
}

void SceneScriptsPanel::setAssetRegistry(const AssetRegistry* registry)
{
  m_assetRegistry = registry;
}

void SceneScriptsPanel::setSceneManager(const SceneManager* sceneManager)
{
  m_sceneManager = sceneManager;
}

void SceneScriptsPanel::setSceneEditCallback(SceneEditCallback callback)
{
  m_sceneEditCallback = std::move(callback);
}

void SceneScriptsPanel::setEditable(const bool editable)
{
  m_editable = editable;
}

void SceneScriptsPanel::open()
{
  m_open = true;
  m_focusRequested = true;
}

bool SceneScriptsPanel::isOpen() const
{
  return m_open;
}

void SceneScriptsPanel::setOpen(const bool open)
{
  m_open = open;
}

void SceneScriptsPanel::send(const nlohmann::json& edit) const
{
  if (m_editable && m_sceneEditCallback)
  {
    m_sceneEditCallback(edit);
  }
}

void SceneScriptsPanel::displayGui()
{
  if (!m_open)
  {
    m_editCopies.clear();
    return;
  }

  if (m_focusRequested)
  {
    ImGui::SetNextWindowFocus();
    m_focusRequested = false;
  }

  bool stayOpen = true;
  if (ImGui::Begin(windowName, &stayOpen))
  {
    const auto scene = m_sceneManager ? m_sceneManager->getCurrentScene() : nullptr;

    // A copy belongs to the scene it was started in; keep it from landing on another scene's script.
    const auto sceneUUID = scene ? std::optional(scene->getUUID()) : std::nullopt;
    if (sceneUUID != m_sceneUUID)
    {
      m_editCopies.clear();
      m_sceneUUID = sceneUUID;
    }

    if (!scene)
    {
      ImGui::TextDisabled("No scene loaded.");
    }
    else
    {
      ImGui::BeginDisabled(!m_editable);
      displayAddButton(*scene);
      ImGui::EndDisabled();

      const auto& scripts = scene->getScripts();

      if (scripts.empty())
      {
        ImGui::TextDisabled("This scene has no scene scripts.");
      }

      // The scene edits below change the list on the next snapshot, not this frame, so the loop is safe.
      for (std::size_t i = 0; i < scripts.size(); ++i)
      {
        const auto& script = scripts[i];
        const std::string className = script->getClassName();

        ImGui::PushID(className.c_str());
        ImGui::Separator();

        ImGui::BeginDisabled(!m_editable);

        ImGui::BeginDisabled(!sceneScriptPanel::reorderTarget(i, scripts.size(), true));
        if (ImGui::ArrowButton("##up", ImGuiDir_Up))
        {
          if (const auto target = sceneScriptPanel::reorderTarget(i, scripts.size(), true))
          {
            send(replication::buildReorderSceneScript(className, *target));
          }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!sceneScriptPanel::reorderTarget(i, scripts.size(), false));
        if (ImGui::ArrowButton("##down", ImGuiDir_Down))
        {
          if (const auto target = sceneScriptPanel::reorderTarget(i, scripts.size(), false))
          {
            send(replication::buildReorderSceneScript(className, *target));
          }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextUnformatted(className.c_str());

        ImGui::SameLine();
        if (ImGui::SmallButton("Remove"))
        {
          m_editCopies.erase(className);
          send(replication::buildRemoveSceneScript(className));
        }

        const auto copy = m_editCopies.find(className);
        nlohmann::json fields = copy != m_editCopies.end() ? copy->second : script->getFields();
        if (drawScriptFieldWidgets(fields, MixedFields{}))
        {
          m_editCopies[className] = std::move(fields);
        }

        ImGui::EndDisabled();
        ImGui::PopID();
      }

      flushEditCopies(*scene);

      if (m_editable)
      {
        displayDropTarget(*scene);
      }
    }
  }

  ImGui::End();

  if (!stayOpen)
  {
    setOpen(false);
  }
}

void SceneScriptsPanel::displayAddButton(const SceneAsset& scene)
{
  if (ImGui::Button("Add Scene Script"))
  {
    ImGui::OpenPopup(addPopupName);
  }

  if (!ImGui::BeginPopup(addPopupName))
  {
    return;
  }

  const auto classes = m_assetRegistry
    ? sceneScriptPanel::availableClasses(*m_assetRegistry, scene.getScripts())
    : std::vector<std::string>{};

  if (classes.empty())
  {
    ImGui::TextDisabled("No script assets left to add.");
  }

  for (const auto& className : classes)
  {
    if (ImGui::Selectable(className.c_str()))
    {
      send(replication::buildAddSceneScript(className));
    }
  }

  ImGui::EndPopup();
}

void SceneScriptsPanel::displayDropTarget(const SceneAsset& scene) const
{
  ImVec2 size = ImGui::GetContentRegionAvail();
  size.y = std::max(size.y, 24.0f);
  ImGui::InvisibleButton("##sceneScriptDropZone", size);

  if (!ImGui::BeginDragDropTarget())
  {
    return;
  }

  if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetDragDrop::script))
  {
    const std::string uuidStr(static_cast<const char*>(payload->Data), static_cast<std::size_t>(payload->DataSize));
    const auto parsed = uuids::uuid::from_string(uuidStr);
    const auto* record = parsed && m_assetRegistry ? m_assetRegistry->getByUUIDOfType(*parsed, AssetType::Script)
                                                   : nullptr;

    if (record && !record->className.empty() && !replication::sceneScriptIndex(scene, record->className))
    {
      send(replication::buildAddSceneScript(record->className));
    }
  }

  ImGui::EndDragDropTarget();
}

void SceneScriptsPanel::flushEditCopies(const SceneAsset& scene)
{
  const bool anyActive = ImGui::IsAnyItemActive();

  for (auto it = m_editCopies.begin(); it != m_editCopies.end();)
  {
    const auto index = replication::sceneScriptIndex(scene, it->first);

    if (!index)
    {
      it = m_editCopies.erase(it);
      continue;
    }

    if (sceneScriptPanel::shouldSendFieldEdit(&it->second, scene.getScripts()[*index]->getFields(), anyActive))
    {
      send(replication::buildSetSceneScriptFields(it->first, it->second));
    }

    it = anyActive ? std::next(it) : m_editCopies.erase(it);
  }
}
