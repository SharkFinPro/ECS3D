#include "EditorApp.h"
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <RenderSystem.h>
#include <EditorTheme.h>
#include <GuiComponents.h>
#include <ViewportGizmo.h>
#include <Gizmo.h>
#include <EditorGizmoSettings.h>
#include <SettingsStore.h>
#include <objects/components/Component.h>
#include <objects/components/Camera.h>
#include <objects/components/PlayerController.h>
#include <PlayerSlots.h>
#include <NetClient.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/imGui/ImGuiInstance.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <objects/Object.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
  // How a camera reads in the editor's "View" combo: the owning object's name, the player slot when it's a
  // client's player camera, and a cue when the Camera is inactive (RenderSystem only renders through active
  // cameras, so selecting one shows the free-fly view instead).
  std::string cameraLabel(const std::shared_ptr<Object>& object)
  {
    std::string label = object->getName();

    if (const auto playerController = object->getComponent<PlayerController>(ComponentType::playerController))
    {
      label += " (Player " + std::to_string(playerController->getPlayerSlot()) + ")";
    }

    if (const auto camera = object->getComponent<Camera>(ComponentType::camera); camera && !camera->isActive())
    {
      label += " - inactive";
    }

    return label;
  }

  // What the "View" combo shows while closed: the chosen camera's label, or the free-fly label when
  // nothing is chosen or the choice is gone.
  std::string cameraPreviewLabel(ObjectManager* objectManager, const std::optional<uuids::uuid>& viewCameraObject,
                                 const char* freeFlyLabel)
  {
    std::string preview = freeFlyLabel;
    if (objectManager && viewCameraObject)
    {
      if (const auto object = objectManager->getObjectByUUID(*viewCameraObject))
      {
        preview = cameraLabel(object);
      }
    }

    return preview;
  }

  // The scene's Camera objects as combo entries; picking one makes it the viewport's camera.
  void selectSceneCamera(ObjectManager* objectManager, std::optional<uuids::uuid>& viewCameraObject)
  {
    if (!objectManager)
    {
      return;
    }

    for (const auto& object : objectManager->getAllObjects())
    {
      if (!object->getComponent<Camera>(ComponentType::camera))
      {
        continue;
      }

      const auto uuid = object->getUUID();

      // Objects can share a name, so the uuid disambiguates the ImGui id.
      const std::string label = cameraLabel(object) + "##" + uuids::to_string(uuid);

      if (ImGui::Selectable(label.c_str(), viewCameraObject == uuid))
      {
        viewCameraObject = uuid;
      }
    }
  }

  // How a player slot reads in the "Player" combo: its number, plus the owning object's name when exactly
  // one object holds that slot.
  std::string playerLabel(ObjectManager* objectManager, const int32_t slot)
  {
    std::string label = "Player " + std::to_string(slot);
    if (!objectManager)
    {
      return label;
    }

    std::string owner;
    int owners = 0;
    for (const auto& object : objectManager->getAllObjects())
    {
      const auto playerController = object->getComponent<PlayerController>(ComponentType::playerController);
      if (playerController && playerController->getPlayerSlot() == slot)
      {
        owner = object->getName();
        ++owners;
      }
    }

    return owners == 1 ? label + " (" + owner + ")" : label;
  }

  const char* sceneStatusLabel(const SceneStatus status)
  {
    return status == SceneStatus::running ? "Running"
         : status == SceneStatus::paused  ? "Paused"
                                          : "Stopped";
  }

  ImVec4 sceneStatusColor(const SceneStatus status)
  {
    return status == SceneStatus::running ? theme::sceneGreen
         : status == SceneStatus::paused  ? theme::scriptAmber
                                          : theme::t3;
  }

  // The Move/Rotate/Scale mode buttons: the active mode is styled like the mockup's accent Start button.
  // Named `viewportGizmo` rather than `gizmo` so it doesn't shadow the `gizmo::` namespace below.
  void displayGizmoModeButtons(ViewportGizmo& viewportGizmo)
  {
    constexpr int modeButtonWidth = 60;

    const auto modeButton = [&](const char* label, const gizmo::Mode mode) {
      const bool active = viewportGizmo.mode() == mode;

      if (active)
      {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(60, 200, 224));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::v4(60, 200, 224));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::onAcc);
      }

      if (ImGui::Button(label, { modeButtonWidth, 0 }))
      {
        viewportGizmo.setMode(mode);
      }

      if (active)
      {
        ImGui::PopStyleColor(4);
      }
    };

    modeButton("Move", gizmo::Mode::translate);
    ImGui::SameLine();
    modeButton("Rotate", gizmo::Mode::rotate);
    ImGui::SameLine();
    modeButton("Scale", gizmo::Mode::scale);
  }
}

void EditorApp::displayMessageLog()
{
  ImGui::Begin("Project Errors");

  if (m_errorMessages.empty())
  {
    // Mockup's reassuring empty state instead of a bare, blank panel.
    gc::successEmptyState("No problems detected");
    ImGui::End();
    return;
  }

  // Count badge + Clear on the header row.
  gc::pill(std::to_string(m_errorMessages.size()).c_str(), theme::t3);
  ImGui::SameLine();
  if (ImGui::Button("Clear"))
  {
    m_errorMessages.clear();
  }

  ImGui::Spacing();

  for (const auto& message : m_errorMessages)
  {
    ImGui::TextWrapped("%s", message.c_str());
  }

  ImGui::End();
}

void EditorApp::displaySceneStatus()
{
  ImGui::Begin("Scene Status");

  displayPlayControls();

  displayRayTracingToggle();

  displayCameraSelector();

  displayPlayerSelector();

  displayGizmoControls();

  displaySceneReadout();

  ImGui::End();
}

void EditorApp::displayPlayControls()
{
  constexpr int sceneStatusButtonWidth = 125;

  // Play controls first (mockup's leading accent Start button).
  if (m_sceneStatus != SceneStatus::running)
  {
    ImGui::BeginDisabled(!m_serverEditable);
    ImGui::PushStyleColor(ImGuiCol_Button, theme::accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(60, 200, 224));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::v4(60, 200, 224));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::onAcc);
    if (ImGui::Button("Start", {sceneStatusButtonWidth, 0}))
    {
      if (m_playHistory.requestStart())
      {
        clearUndoRedoPending();
      }

      sendSceneControl(net::SceneControlOp::start);
    }
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
  }
  else
  {
    ImGui::BeginDisabled(!m_serverEditable);
    if (ImGui::Button("Pause", {sceneStatusButtonWidth, 0}))
    {
      sendSceneControl(net::SceneControlOp::pause);
    }
    ImGui::EndDisabled();
  }

  if (m_sceneStatus != SceneStatus::stopped)
  {
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_serverEditable);
    if (ImGui::Button("Stop", {sceneStatusButtonWidth, 0}))
    {
      if (m_playHistory.requestStop())
      {
        clearUndoRedoPending();
      }

      sendSceneControl(net::SceneControlOp::stop);
    }
    ImGui::EndDisabled();
  }
}

void EditorApp::displayRayTracingToggle() const
{
  // Ray tracing toggle: a local render setting (this view's vke renderer only, never replicated), so
  // it stays enabled on a read-only server. Greyed out when the device can't ray trace.
  const auto renderingManager = m_renderer->getRenderingManager();
  ImGui::SameLine(0.0f, 18.0f);
  ImGui::BeginDisabled(!renderingManager->supportsRayTracing());
  bool rayTracing = renderingManager->isRayTracingEnabled();
  if (gc::accentCheckboxCompact("Ray Tracing", &rayTracing))
  {
    if (rayTracing)
    {
      renderingManager->enableRayTracing();
    }
    else
    {
      renderingManager->disableRayTracing();
    }
  }
  ImGui::EndDisabled();
}

void EditorApp::displaySceneReadout() const
{
  // Status readout (divider + dot/label + scene name), right-aligned to the panel edge so the play
  // buttons stay put on the left regardless of the readout's width.
  const char* label = sceneStatusLabel(m_sceneStatus);
  const ImVec4 dotCol = sceneStatusColor(m_sceneStatus);
  const auto scene = m_sceneManager->getCurrentScene();

  constexpr float nameGap = 14.0f;     // scene name -> divider
  constexpr float dividerGap = 14.0f;  // divider -> dot block
  constexpr float dotToLabel = 18.0f;  // dot block start -> label text
  float readoutWidth = dividerGap + dotToLabel + ImGui::CalcTextSize(label).x;
  if (scene)
  {
    readoutWidth += ImGui::CalcTextSize(scene->getName().c_str()).x + nameGap;
  }

  // Anchor to the right edge, but never overlap the play buttons on a narrow panel.
  ImGui::SameLine();
  const float readoutX = std::max(ImGui::GetCursorPosX() + 4.0f,
                                  ImGui::GetContentRegionMax().x - readoutWidth);
  ImGui::SetCursorPosX(readoutX);

  // Current scene name (muted) first, mirroring the mockup's toolbar.
  if (scene)
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::t3, "%s", scene->getName().c_str());
    ImGui::SameLine(0.0f, nameGap);
  }

  // Divider between the scene name and the status readout.
  const ImVec2 dp = ImGui::GetCursorScreenPos();
  const float frameH = ImGui::GetFrameHeight();
  ImGui::GetWindowDrawList()->AddLine(ImVec2(dp.x, dp.y + frameH * 0.2f),
                                      ImVec2(dp.x, dp.y + frameH * 0.8f), theme::u32(theme::line));
  ImGui::SetCursorScreenPos(ImVec2(dp.x + dividerGap, dp.y));

  // Status indicator dot + label (green running / amber paused / muted stopped).
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float cy = p.y + ImGui::GetFrameHeight() * 0.5f;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5.0f, cy), 4.0f, theme::u32(dotCol));
    ImGui::SetCursorScreenPos(ImVec2(p.x + dotToLabel, p.y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::t2, "%s", label);
  }
}

void EditorApp::displayCameraSelector()
{
  constexpr auto freeFlyLabel = "Editor Camera";

  const auto scene = m_sceneManager->getCurrentScene();
  const auto objectManager = scene ? scene->getObjectManager().get() : nullptr;

  // A view choice, not a scene edit, so this stays enabled on a read-only server.
  const std::string preview = cameraPreviewLabel(objectManager, m_viewCameraObject, freeFlyLabel);

  ImGui::SameLine(0.0f, 18.0f);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(theme::t2, "%s", "View");
  ImGui::SameLine();

  ImGui::SetNextItemWidth(200.0f);
  if (ImGui::BeginCombo("##ViewCamera", preview.c_str()))
  {
    if (ImGui::Selectable(freeFlyLabel, !m_viewCameraObject))
    {
      m_viewCameraObject.reset();
    }

    selectSceneCamera(objectManager, m_viewCameraObject);

    ImGui::EndCombo();
  }
}

void EditorApp::displayPlayerSelector()
{
  const auto scene = m_sceneManager->getCurrentScene();
  const auto objectManager = scene ? scene->getObjectManager().get() : nullptr;

  std::vector<int32_t> slots;
  if (objectManager)
  {
    slots = playerSlotsInScene(*objectManager);
  }

  if (m_playerSlot >= 0 && std::ranges::find(slots, m_playerSlot) == slots.end())
  {
    slots.insert(std::ranges::upper_bound(slots, m_playerSlot), m_playerSlot);
  }

  const std::string preview = m_playerSlot >= 0 ? "Player " + std::to_string(m_playerSlot) : std::string("-");

  ImGui::SameLine(0.0f, 18.0f);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(theme::t2, "%s", "Player");
  ImGui::SameLine();

  // A view/input choice, not a scene edit, so this stays enabled on a read-only server. It waits for the
  // server to answer the previous request before offering another.
  ImGui::BeginDisabled(!m_netClient->isConnected() || m_playerSlot < 0 || m_requestedPlayerSlot.has_value());
  ImGui::SetNextItemWidth(160.0f);
  if (ImGui::BeginCombo("##PlayerSlot", preview.c_str()))
  {
    for (const int32_t slot : slots)
    {
      const std::string label = playerLabel(objectManager, slot) + "##slot" + std::to_string(slot);

      if (ImGui::Selectable(label.c_str(), slot == m_playerSlot) && slot != m_playerSlot)
      {
        requestPlayerSlot(slot);
      }
    }

    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
}

void EditorApp::displayGizmoControls() const
{
  ImGui::SameLine(0.0f, 18.0f);
  ImGui::BeginDisabled(!m_serverEditable);

  displayGizmoModeButtons(*m_viewportGizmo);

  ImGui::SameLine(0.0f, 14.0f);

  // Scale is always local (it's applied in the object's own axes), so the toggle would have nothing to
  // change there.
  const bool scaleMode = m_viewportGizmo->mode() == gizmo::Mode::scale;
  bool local = m_viewportGizmo->space() == gizmo::Space::local;

  ImGui::BeginDisabled(scaleMode);
  const bool spaceChanged = ImGui::Checkbox("Local", &local);
  const bool spaceHovered = ImGui::IsItemHovered();
  ImGui::EndDisabled();

  if (spaceChanged)
  {
    m_viewportGizmo->setSpace(local ? gizmo::Space::local : gizmo::Space::world);
  }

  if (scaleMode && spaceHovered)
  {
    ImGui::SetTooltip("Scale always uses the object's own axes.");
  }

  ImGui::SameLine(0.0f, 14.0f);

  bool snap = editorGizmoSettings::readSnapEnabled(*m_settings);
  if (gc::accentCheckboxCompact("Snap", &snap))
  {
    editorGizmoSettings::writeSnapEnabled(*m_settings, snap);
  }

  ImGui::EndDisabled();
}
