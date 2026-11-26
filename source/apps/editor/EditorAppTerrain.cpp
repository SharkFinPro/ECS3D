#include "EditorApp.h"
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/Object.h>
#include <objects/components/Component.h>
#include <objects/components/Transform.h>
#include <objects/components/collisions/Terrain.h>
#include <terrain/TerrainBinding.h>
#include <terrain/TerrainBrush.h>
#include <terrain/TerrainRaycast.h>
#include <terrain/TerrainStore.h>
#include <terrain/TerrainVolume.h>
#include <EditorTerrainSettings.h>
#include <GuiComponents.h>
#include <Gizmo.h>
#include <NetClient.h>
#include <ProfileScope.h>
#include <EditorTheme.h>
#include <Selection.h>
#include <SettingsStore.h>
#include <TerrainCursor.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/Renderer3D.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {
  constexpr std::array<const char*, terrainToolCount> toolLabels = { "Add", "Subtract", "Smooth", "Flatten" };

  const std::array<glm::vec4, terrainToolCount> toolColors = {
    glm::vec4(0.35f, 0.9f, 0.45f, 1.0f),
    glm::vec4(0.95f, 0.4f, 0.35f, 1.0f),
    glm::vec4(0.4f, 0.7f, 1.0f, 1.0f),
    glm::vec4(1.0f, 0.75f, 0.3f, 1.0f)
  };

  constexpr float radiusGrowFactor = 1.25f;
  constexpr float radiusShrinkFactor = 0.8f;

  [[nodiscard]] size_t toolIndex(const TerrainTool tool)
  {
    return static_cast<size_t>(tool);
  }
}

std::shared_ptr<Terrain> EditorApp::selectedTerrain() const
{
  if (m_selection->kind() != EditorSelection::Kind::Object || m_selection->size() != 1)
  {
    return nullptr;
  }

  const auto scene = m_sceneManager->getCurrentScene();
  const auto object = scene ? scene->getObjectManager()->getObjectByUUID(*m_selection->objectUUID()) : nullptr;
  const auto component = object ? object->getComponent<Terrain>(ComponentType::collider) : nullptr;

  if (!component || !m_terrainStore->find(component->getTerrainUUID()))
  {
    return nullptr;
  }

  return component;
}

TerrainModeContext EditorApp::terrainModeContext() const
{
  TerrainModeContext context;
  context.editable = m_serverEditable;
  context.sceneStopped = m_sceneStatus == SceneStatus::stopped;

  if (selectedTerrain())
  {
    context.selectedTerrainObject = m_selection->objectUUID();
  }

  return context;
}

void EditorApp::loadTerrainTool()
{
  const size_t stored = editorTerrainSettings::readLastTool(*m_settings);
  m_terrainTool.setTool(static_cast<TerrainTool>(stored < terrainToolCount ? stored : 0));
}

void EditorApp::refreshTerrainMode()
{
  m_terrainBrush.expireAwaiting(std::chrono::steady_clock::now());

  const bool left = m_terrainTool.refresh(terrainModeContext());

  if (left || !m_terrainTool.active())
  {
    endTerrainStroke();
  }
}

void EditorApp::toggleTerrainMode()
{
  if (!m_terrainTool.toggle(terrainModeContext()))
  {
    endTerrainStroke();
  }
}

void EditorApp::setTerrainTool(const TerrainTool tool)
{
  m_terrainTool.setTool(tool);
  editorTerrainSettings::writeLastTool(*m_settings, toolIndex(tool));
}

void EditorApp::scaleTerrainBrushRadius(const float factor)
{
  const size_t tool = toolIndex(m_terrainTool.tool());
  editorTerrainSettings::BrushSettings brush = editorTerrainSettings::readBrush(*m_settings, tool);
  brush.radius = editorTerrainSettings::clampRadius(brush.radius * factor);
  editorTerrainSettings::writeBrush(*m_settings, tool, brush);
}

void EditorApp::growTerrainBrush()
{
  scaleTerrainBrushRadius(radiusGrowFactor);
}

void EditorApp::shrinkTerrainBrush()
{
  scaleTerrainBrushRadius(radiusShrinkFactor);
}

void EditorApp::sendTerrainEdit(const net::Message& message) const
{
  m_netClient->send(message);
}

void EditorApp::endTerrainStroke()
{
  if (!m_terrainBrush.active())
  {
    return;
  }

  const uuids::uuid terrainUuid = m_terrainBrush.terrain();
  static_cast<void>(m_terrainBrush.endFor(m_terrainStore->find(terrainUuid), m_serverEditable,
                                          [this](const net::Message& message) { sendTerrainEdit(message); }));
  terrain::invalidateTerrainBounds(*m_sceneManager, terrainUuid);
}

namespace {
  struct TerrainTarget
  {
    std::shared_ptr<Terrain> component;
    std::shared_ptr<Transform> transform;
    std::shared_ptr<terrain::TerrainVolume> volume;
  };

  [[nodiscard]] TerrainTarget findTerrainTarget(const SceneManager& scenes, const terrain::TerrainStore& store,
                                                const uuids::uuid& objectUuid)
  {
    const auto scene = scenes.getCurrentScene();
    const auto object = scene ? scene->getObjectManager()->getObjectByUUID(objectUuid) : nullptr;

    TerrainTarget target;
    target.component = object ? object->getComponent<Terrain>(ComponentType::collider) : nullptr;
    target.transform = object ? object->getComponent<Transform>(ComponentType::transform) : nullptr;
    target.volume = target.component ? store.find(target.component->getTerrainUUID()) : nullptr;
    return target;
  }

  [[nodiscard]] TerrainFrame frameOf(const Transform& transform)
  {
    TerrainFrame frame;
    frame.position = transform.getPosition();
    frame.orientation = transform.getOrientation();
    return frame;
  }

  [[nodiscard]] std::optional<terrain::RayHit> raycastCursor(const TerrainFrame& frame,
                                                             const terrain::TerrainVolume& volume,
                                                             const gizmo::View& view)
  {
    const ImGuiIO& io = ImGui::GetIO();
    const gizmo::Ray ray = rayToLocal(frame, gizmo::mouseRay(view, glm::vec2(io.MousePos.x, io.MousePos.y)));
    return terrain::raycast(volume, ray.origin, ray.direction, view.farPlane);
  }

  void drawBrushRing(vke::Renderer3D& renderer3D, const TerrainFrame& frame, const terrain::RayHit& hit, const float radius,
                     const glm::vec4& color)
  {
    const terrain::RayHit worldHit = hitToWorld(frame, hit);
    const std::vector<glm::vec3> ring = brushRing(worldHit.point, worldHit.normal, radius);

    for (size_t index = 0; index < ring.size(); ++index)
    {
      renderer3D.renderLine(ring[index], ring[(index + 1) % ring.size()], color, 2.0f, 0.0f, vke::DebugDepth::xray);
    }
  }
}

void EditorApp::drawTerrainSculpt(const std::shared_ptr<Terrain>& component)
{
  if (!component || component != selectedTerrain())
  {
    return;
  }

  drawTerrainSculptToggle();

  if (!m_terrainTool.active())
  {
    return;
  }

  drawTerrainToolStrip();
  drawTerrainBrushControls(component);
}

void EditorApp::drawTerrainSculptToggle()
{
  ImGui::BeginDisabled(!m_serverEditable);

  bool active = m_terrainTool.active();
  if (gc::accentCheckbox("Sculpt", &active) && active != m_terrainTool.active())
  {
    toggleTerrainMode();
  }

  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip("%s", "Shape the terrain with a brush: left drag sculpts, right drag still flies the camera. "
                            "Picking and the transform gizmo are off while sculpting.");
  }

  ImGui::EndDisabled();
}

void EditorApp::drawTerrainToolStrip()
{
  const size_t current = toolIndex(m_terrainTool.tool());

  for (size_t tool = 0; tool < terrainToolCount; ++tool)
  {
    if (tool > 0)
    {
      ImGui::SameLine();
    }

    if (ImGui::RadioButton(toolLabels[tool], tool == current))
    {
      setTerrainTool(static_cast<TerrainTool>(tool));
    }
  }
}

void EditorApp::drawTerrainBrushControls(const std::shared_ptr<Terrain>& component)
{
  const size_t current = toolIndex(m_terrainTool.tool());
  editorTerrainSettings::BrushSettings brush = editorTerrainSettings::readBrush(*m_settings, current);
  bool edited = false;

  edited |= gc::labeledDrag("Radius", &brush.radius, 0.25f);
  edited |= gc::accentSlider("Strength", &brush.strength, editorTerrainSettings::minStrength,
                             editorTerrainSettings::maxStrength);
  edited |= gc::accentSlider("Falloff", &brush.falloff, editorTerrainSettings::minFalloff,
                             editorTerrainSettings::maxFalloff);

  if (edited)
  {
    brush.radius = editorTerrainSettings::clampRadius(brush.radius);
    brush.strength = editorTerrainSettings::clampStrength(brush.strength);
    brush.falloff = editorTerrainSettings::clampFalloff(brush.falloff);
    editorTerrainSettings::writeBrush(*m_settings, current, brush);
  }

  if (const auto volume = m_terrainStore->find(component->getTerrainUUID());
      volume && brush.radius > effectiveBrushRadius(brush.radius, *volume))
  {
    ImGui::TextColored(theme::t2, "Radius is limited to %.1f for this terrain's voxel size.",
                       effectiveBrushRadius(brush.radius, *volume));
  }
}

void EditorApp::driveTerrainBrush(const uuids::uuid& terrainUuid, const std::shared_ptr<terrain::TerrainVolume>& volume,
                                  const TerrainPointer& pointer, const std::optional<terrain::RayHit>& hit)
{
  const size_t tool = toolIndex(m_terrainTool.tool());

  const TerrainBrushSession::Advance advanced = m_terrainBrush.advance(
    terrainUuid, volume, m_serverEditable, pointer, hit,
    [this, tool] {
      return terrainBrushTemplate(m_terrainTool.tool(), editorTerrainSettings::readBrush(*m_settings, tool));
    },
    [this](const net::Message& message) { sendTerrainEdit(message); });

  if (advanced.changed || advanced.ended)
  {
    terrain::invalidateTerrainBounds(*m_sceneManager, terrainUuid);
  }
}

void EditorApp::updateTerrainMode(const gizmo::View& view, const bool sceneHovered)
{
  const bool pressed = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  const bool wasPressed = std::exchange(m_terrainPointerWasDown, pressed);

  if (!m_terrainTool.active())
  {
    return;
  }

  ECS3D_PROFILE_SCOPE("Terrain mode");

  const TerrainTarget target = findTerrainTarget(*m_sceneManager, *m_terrainStore, *m_terrainTool.terrainObject());

  if (!target.volume || !target.transform)
  {
    endTerrainStroke();
    return;
  }

  const auto rendering = m_renderer->getRenderingManager();
  const TerrainFrame frame = frameOf(*target.transform);

  std::optional<terrain::RayHit> hit;
  if (sceneHovered && !m_viewCameraObject.has_value())
  {
    hit = raycastCursor(frame, *target.volume, view);
  }

  const TerrainPointer pointer = makeTerrainPointer(pressed, wasPressed, sceneHovered, m_viewCameraObject.has_value(),
                                                    rendering->isSceneRightDragged(), hit.has_value());
  driveTerrainBrush(target.component->getTerrainUUID(), target.volume, pointer, hit);

  if (!hit || pointer.flying)
  {
    return;
  }

  const size_t tool = toolIndex(m_terrainTool.tool());
  const editorTerrainSettings::BrushSettings brush = editorTerrainSettings::readBrush(*m_settings, tool);
  drawBrushRing(*rendering->getRenderer3D(), frame, *hit, effectiveBrushRadius(brush.radius, *target.volume),
                toolColors[tool]);
}
