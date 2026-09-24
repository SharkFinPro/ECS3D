#include "EditorApp.h"
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <RenderSystem.h>
#include <EditorTheme.h>
#include <GuiComponents.h>
#include <objects/components/Component.h>
#include <objects/components/Camera.h>
#include <objects/components/PlayerController.h>
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

  displaySceneReadout();

  ImGui::End();
}

void EditorApp::displayPlayControls() const
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
