#include "SceneScriptsPanel.h"
#include "AssetDragDrop.h"
#include "MixedFields.h"
#include "SceneScriptsPanelLogic.h"
#include "components/ScriptEditor.h"
#include <assets/AssetRegistry.h>
#include <assets/ScriptClassAssets.h>
#include <scenes/SceneAsset.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneScriptEdits.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

void SceneScriptsPanel::sendStructural(const nlohmann::json& edit, const SceneAsset& scene)
{
  if (m_structuralPending)
  {
    return;
  }

  m_structuralGate.begin(classOrder(&scene));
  m_structuralPending = true;
  send(edit);
}

std::vector<std::string> SceneScriptsPanel::classOrder(const SceneAsset* scene)
{
  std::vector<std::string> order;

  if (scene)
  {
    for (const auto& script : scene->getScripts())
    {
      order.push_back(script->getClassName());
    }
  }

  return order;
}

void SceneScriptsPanel::syncSceneTracking(const std::shared_ptr<SceneAsset>& scene)
{
  // A copy belongs to the scene it was started in; keep it from landing on another scene's script.
  const auto sceneUUID = scene ? std::optional(scene->getUUID()) : std::nullopt;
  if (sceneUUID != m_sceneUUID)
  {
    m_fieldEdits.clear();
    m_structuralGate.clear();
    m_sceneUUID = sceneUUID;
  }

  m_structuralPending = m_structuralGate.blocked(classOrder(scene.get()));
}

void SceneScriptsPanel::displayGui()
{
  const auto scene = m_sceneManager ? m_sceneManager->getCurrentScene() : nullptr;
  syncSceneTracking(scene);

  if (m_open)
  {
    if (m_focusRequested)
    {
      ImGui::SetNextWindowFocus();
      m_focusRequested = false;
    }

    bool stayOpen = true;
    if (ImGui::Begin(windowName, &stayOpen))
    {
      displayContents(scene.get());
    }

    ImGui::End();

    if (!stayOpen)
    {
      setOpen(false);
    }
  }

  // Runs while the window is collapsed, hidden behind another tab or closed, so a finished edit is
  // still sent. A copy a widget still holds cannot be finished, so a closed panel drops it.
  flushFieldEdits(scene.get());

  if (!m_open)
  {
    m_fieldEdits.clear();
  }
}

void SceneScriptsPanel::displayContents(const SceneAsset* scene)
{
  if (!scene)
  {
    ImGui::TextDisabled("No scene loaded.");
    return;
  }

  displayAddControls(*scene);

  if (scene->getScripts().empty())
  {
    ImGui::TextDisabled("This scene has no scene scripts.");
  }

  // Edits change the list on the next snapshot, not this frame, so indexing the list here is safe.
  for (std::size_t i = 0; i < scene->getScripts().size(); ++i)
  {
    displayRow(*scene, i);
  }

  if (m_editable)
  {
    displayDropTarget(*scene);
  }
}

void SceneScriptsPanel::displayArrowButton(const SceneAsset& scene, const std::size_t index, const bool up)
{
  const auto target = sceneScriptPanel::reorderTarget(index, scene.getScripts().size(), up);

  ImGui::BeginDisabled(!target);
  if (ImGui::ArrowButton(up ? "##up" : "##down", up ? ImGuiDir_Up : ImGuiDir_Down) && target)
  {
    sendStructural(replication::buildReorderSceneScript(scene.getScripts()[index]->getClassName(), *target), scene);
  }
  ImGui::EndDisabled();
}

void SceneScriptsPanel::displayRow(const SceneAsset& scene, const std::size_t index)
{
  const auto& script = scene.getScripts()[index];
  const std::string className = script->getClassName();

  ImGui::PushID(className.c_str());
  ImGui::Separator();

  ImGui::BeginDisabled(!m_editable || m_structuralPending);

  displayArrowButton(scene, index, true);
  ImGui::SameLine();
  displayArrowButton(scene, index, false);

  ImGui::SameLine();
  ImGui::TextUnformatted(className.c_str());

  ImGui::SameLine();
  if (ImGui::SmallButton("Remove"))
  {
    m_fieldEdits.erase(className);
    sendStructural(replication::buildRemoveSceneScript(className), scene);
  }

  ImGui::EndDisabled();

  ImGui::BeginDisabled(!m_editable);

  const nlohmann::json* shown = m_fieldEdits.shown(className);
  nlohmann::json fields = shown ? *shown : script->getFields();
  if (drawScriptFieldWidgets(fields, MixedFields{}))
  {
    m_fieldEdits.edit(className, std::move(fields));
  }

  ImGui::EndDisabled();
  ImGui::PopID();
}

void SceneScriptsPanel::displayAddControls(const SceneAsset& scene)
{
  ImGui::BeginDisabled(!m_editable || m_structuralPending);

  if (ImGui::Button("Add Scene Script"))
  {
    ImGui::OpenPopup(addPopupName);
  }

  if (ImGui::BeginPopup(addPopupName))
  {
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
        sendStructural(replication::buildAddSceneScript(className), scene);
      }
    }

    ImGui::EndPopup();
  }

  ImGui::EndDisabled();
}

void SceneScriptsPanel::displayDropTarget(const SceneAsset& scene)
{
  const ImRect windowRect = ImGui::GetCurrentWindow()->Rect();
  if (!ImGui::BeginDragDropTargetCustom(windowRect, ImGui::GetID("##sceneScriptsDrop")))
  {
    return;
  }

  if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetDragDrop::script))
  {
    const std::string uuidStr(static_cast<const char*>(payload->Data), static_cast<std::size_t>(payload->DataSize));
    const auto parsed = uuids::uuid::from_string(uuidStr);
    const auto* record = parsed && m_assetRegistry ? m_assetRegistry->getByUUIDOfType(*parsed, AssetType::Script)
                                                   : nullptr;
    if (record && !usableOnScene(*record))
    {
      record = nullptr;
    }

    if (record && !record->className.empty() && !replication::sceneScriptIndex(scene, record->className))
    {
      sendStructural(replication::buildAddSceneScript(record->className), scene);
    }
  }

  ImGui::EndDragDropTarget();
}

void SceneScriptsPanel::flushFieldEdits(const SceneAsset* scene)
{
  const auto replicatedFor = [scene](const std::string& className) -> const nlohmann::json* {
    const auto index = scene ? replication::sceneScriptIndex(*scene, className) : std::nullopt;
    return index ? &scene->getScripts()[*index]->getFields() : nullptr;
  };

  for (const auto& [className, fields] : m_fieldEdits.poll(replicatedFor, ImGui::IsAnyItemActive()))
  {
    send(replication::buildSetSceneScriptFields(className, fields));
  }
}
