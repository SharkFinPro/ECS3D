#include "EditorApp.h"
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <ObjectGUIManager.h>
#include <InspectorPanel.h>
#include <EditorTheme.h>
#include <AssetBrowserPanel.h>
#include <SaveUI.h>
#include <SettingsPanel.h>
#include <ConsolePanel.h>
#include <ProfilerCapture.h>
#include <ProfilerPanel.h>
#include <SceneScriptsPanel.h>
#include <SettingsStore.h>
#include <SceneStatusLayout.h>
#include <DockLayoutFit.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/imGui/ImGuiInstance.h>
#include <objects/Object.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <optional>
#include <vector>
#include <imgui_internal.h>

void EditorApp::updateGui()
{
  // Drawn regardless of the GUI toggle below: a window-close request can arrive while the GUI is
  // hidden, and the prompt must not be hideable out from under the user.
  m_saveUI->displayUnsavedChangesModal();

  if (!m_shouldDisplayGui)
  {
    return;
  }

  // Propagate the server's editability so the panels disable their mutating affordances (and show a
  // read-only cue) on a non-edit server.
  m_objectGUIManager->setEditable(m_serverEditable);
  m_inspectorPanel->setEditable(m_serverEditable);
  m_assetBrowser->setEditable(m_serverEditable);
  m_sceneScriptsPanel->setEditable(m_serverEditable);
  m_saveUI->setEditable(m_serverEditable);

  displayMenuBar();

  updateDockSpace();

  m_assetBrowser->displayGui();

  displayMessageLog();

  displaySceneStatus();

  // The object tree + Inspector panels (always drawn so they stay present/dockable; empty when no scene
  // is loaded yet). Edits fire the callbacks wired in the ctor.
  const auto scene = m_sceneManager->getCurrentScene();
  const auto* objectManager = scene ? scene->getObjectManager().get() : nullptr;
  const auto activeSceneUUID = scene ? std::optional(scene->getUUID()) : std::nullopt;
  m_objectGUIManager->displayGui(objectManager);
  m_inspectorPanel->setSceneStopped(m_sceneStatus == SceneStatus::stopped);
  m_inspectorPanel->displayGui(objectManager, activeSceneUUID);

  // Draws nothing while closed. Preferences are local, so it needs no scene and no server.
  m_settingsPanel->displayGui();

  m_sceneScriptsPanel->displayGui();

  m_consolePanel->displayGui();

  CaptureInput captureEnvironment;
  captureEnvironment.app = "editor";
  captureEnvironment.engineVersion = ECS3D_ENGINE_VERSION;
  captureEnvironment.buildType = captureBuildType();
  captureEnvironment.platform = capturePlatform();
  captureEnvironment.sceneName = scene ? scene->getName() : std::string();
  captureEnvironment.objectCount = objectManager ? objectManager->getAllObjects().size() : 0;
  m_profilerPanel->setCaptureEnvironment(captureEnvironment);
  m_profilerPanel->displayGui();

  // Scenes are browsed/switched from the "Assets" panel (double-click a scene tile), not a separate
  // scene-selector widget.
}

void EditorApp::displayMenuBar()
{
  if (ImGui::BeginMainMenuBar())
  {
    m_renderer->getImGuiInstance()->setMenuBarHeight(ImGui::GetWindowSize().y);

    if (ImGui::BeginMenu("File"))
    {
      // Save serializes the replicated project to disk; New/Open send it to the server (which reloads +
      // re-snapshots). New/Open are mutations, disabled on a read-only server; Save stays available.
      ImGui::BeginDisabled(!m_serverEditable);
      if (ImGui::MenuItem("New"))
      {
        m_saveUI->requestNewProject();
      }

      if (ImGui::MenuItem("Open"))
      {
        m_saveUI->requestOpen();
      }
      ImGui::EndDisabled();

      ImGui::Separator();

      if (ImGui::MenuItem("Save", "Ctrl+S"))
      {
        static_cast<void>(m_saveUI->save());
      }

      if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
      {
        m_saveUI->saveAs();
      }

      ImGui::EndMenu();
    }

    displayEditMenu();

    m_assetBrowser->displayMenuWidget();

    displayWindowMenu();

    // A persistent read-only badge, right-aligned, whenever the connected server isn't in edit mode.
    if (!m_serverEditable)
    {
      const char* badge = "READ-ONLY (server not in edit mode)";
      const float badgeWidth = ImGui::CalcTextSize(badge).x;
      ImGui::SameLine(ImGui::GetWindowWidth() - badgeWidth - ImGui::GetStyle().WindowPadding.x * 2.0f);
      ImGui::TextColored(theme::scriptAmber, "%s", badge);
    }

    ImGui::EndMainMenuBar();
  }
}

void EditorApp::displayWindowMenu() const
{
  if (ImGui::BeginMenu("Window"))
  {
    // Checked rather than a plain item, so the menu says whether the panel is already up: it is a
    // dockable window that may be sitting behind another tab rather than closed.
    if (ImGui::MenuItem("Settings", nullptr, m_settingsPanel->isOpen()))
    {
      if (m_settingsPanel->isOpen())
      {
        m_settingsPanel->setOpen(false);
      }
      else
      {
        m_settingsPanel->open();
      }
    }

    if (ImGui::MenuItem("Scene Scripts", nullptr, m_sceneScriptsPanel->isOpen()))
    {
      if (m_sceneScriptsPanel->isOpen())
      {
        m_sceneScriptsPanel->setOpen(false);
      }
      else
      {
        m_sceneScriptsPanel->open();
      }
    }

    if (ImGui::MenuItem("Console", nullptr, m_consolePanel->isOpen()))
    {
      if (m_consolePanel->isOpen())
      {
        m_consolePanel->setOpen(false);
      }
      else
      {
        m_consolePanel->open();
      }
    }

    if (ImGui::MenuItem("Profiler", nullptr, m_profilerPanel->isOpen()))
    {
      m_profilerPanel->toggle();
    }

    ImGui::EndMenu();
  }
}

namespace
{
// A side node is a child whose sibling holds the central node: ImGui sizes it from its own SizeRef and gives
// the sibling the remainder, which is why it keeps its pixels when the window shrinks.
void collectDockSides(ImGuiDockNode* node, std::vector<ImGuiDockNode*>& nodes, std::vector<DockSideSize>& sides)
{
  if (node->IsLeafNode() || !node->ChildNodes[0]->IsVisible || !node->ChildNodes[1]->IsVisible)
  {
    return;
  }

  const int axis = node->SplitAxis;

  for (int child = 0; child < 2; ++child)
  {
    ImGuiDockNode* side = node->ChildNodes[child];
    const ImGuiDockNode* sibling = node->ChildNodes[1 - child];

    if (sibling->HasCentralNodeChild && !side->HasCentralNodeChild)
    {
      nodes.push_back(side);
      sides.push_back(DockSideSize{side->ID, axis, side->SizeRef[axis]});
    }

    collectDockSides(side, nodes, sides);
  }
}
}

void EditorApp::updateDockSpace()
{
  static bool dockPercentsSetup = false;
  static bool dockLocationsSetup = false;

  // A restored layout already holds the dock tree and splits; only the per-panel floors are not saved in it.
  if (m_layoutRestored)
  {
    static bool minimumSizesSetup = false;

    if (!minimumSizesSetup)
    {
      applyDockMinimumSizes();

      minimumSizesSetup = true;
    }

    fitDockToViewport();

    return;
  }

  if (!dockLocationsSetup && dockPercentsSetup)
  {
    applyDockLocations();

    dockLocationsSetup = true;
  }

  if (!dockPercentsSetup)
  {
    dockPercentsSetup = applyDockPercents();

    // The engine rebuilds the default tree on the frame after the percents change, so the proportions are
    // taken from the rebuilt tree.
    m_dockSettleFrames = dockPercentsSetup ? 3 : 0;
  }

  fitDockToViewport();
}

void EditorApp::fitDockToViewport()
{
  ImGuiDockNode* root = ImGui::DockBuilderGetNode(ImGui::GetID("WindowDockSpace"));

  if (root == nullptr)
  {
    return;
  }

  std::vector<ImGuiDockNode*> nodes;
  std::vector<DockSideSize> sides;
  collectDockSides(root, nodes, sides);

  // A splitter drag or a tab drag in the dockspace owns the layout until it ends.
  const ImGuiContext& context = *ImGui::GetCurrentContext();
  const bool userDragging = context.ActiveId != 0 && root->HostWindow != nullptr &&
                            context.ActiveIdWindow == root->HostWindow;
  const bool layoutChanged = m_dockSettleFrames > 0 || !m_dockProportions.tracks(sides);

  if (m_dockSettleFrames > 0)
  {
    --m_dockSettleFrames;
  }

  switch (m_dockResizeGate.update(root->Size.x, root->Size.y, userDragging, layoutChanged))
  {
  case DockLayoutStep::remember:
    m_dockProportions.remember(sides, root->Size.x, root->Size.y);
    break;

  case DockLayoutStep::rescale:
    for (const auto& [id, axis, size] : m_dockProportions.fit(root->Size.x, root->Size.y))
    {
      const auto node = std::ranges::find(nodes, id, &ImGuiDockNode::ID);

      if (node != nodes.end())
      {
        (*node)->SizeRef[axis] = size;
      }
    }

    applyDockPanelMinimumSizes();
    break;

  case DockLayoutStep::none:
    break;
  }
}

void EditorApp::applyDockLocations() const
{
  const auto gui = m_renderer->getImGuiInstance();

  gui->dockCenter(m_sceneViewName.c_str());

  gui->dockLeft("Objects");

  gui->dockRight("Inspector");
  gui->dockRight("Scene Scripts");

  gui->dockTop("Scene Status");

  gui->dockBottom("Assets");
  gui->dockBottom("Project Errors");
  gui->dockBottom("Console");
  gui->dockBottom("Profiler");

  applyDockMinimumSizes();
}

void EditorApp::applyDockMinimumSizes() const
{
  const auto gui = m_renderer->getImGuiInstance();

  gui->setDockedWindowMinimumSize("Scene Status", ImVec2{0.0f, sceneStatusBaseMinimumHeight});

  applyDockPanelMinimumSizes();
}

void EditorApp::applyDockPanelMinimumSizes() const
{
  const auto gui = m_renderer->getImGuiInstance();

  // Unscaled pixels; the engine applies content scale. Each floor is on one axis so the splitter can still
  // shrink the other, and tabs sharing a dock node take the largest floor among them. A window too small for
  // the full floors lowers them, so the scene view keeps a share of it.
  constexpr float maxSideFloorFraction = 0.25f;
  constexpr float maxBottomFloorFraction = 0.3f;

  const float contentScale = std::max(m_renderer->getWindow()->getContentScale(), 0.01f);
  const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
  const float width = viewportSize.x / contentScale;
  const float height = viewportSize.y / contentScale;

  const auto sideFloor = [&](const float base)
  {
    return ImVec2{dockFloorForExtent(base, width, maxSideFloorFraction), 0.0f};
  };

  const auto bottomFloor = [&](const float base)
  {
    return ImVec2{0.0f, dockFloorForExtent(base, height, maxBottomFloorFraction)};
  };

  gui->setDockedWindowMinimumSize("Objects", sideFloor(260.0f));
  gui->setDockedWindowMinimumSize("Inspector", sideFloor(310.0f));
  gui->setDockedWindowMinimumSize("Scene Scripts", sideFloor(310.0f));
  gui->setDockedWindowMinimumSize("Assets", bottomFloor(200.0f));
  gui->setDockedWindowMinimumSize("Project Errors", bottomFloor(90.0f));
  gui->setDockedWindowMinimumSize("Console", bottomFloor(120.0f));
  gui->setDockedWindowMinimumSize("Profiler", bottomFloor(120.0f));
}

bool EditorApp::applyDockPercents() const
{
  const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;

  // A just-opened (or minimized) window can report a zero-size viewport on its first frames; wait for a
  // real size instead of dividing by zero or locking in a degenerate layout.
  if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
  {
    return false;
  }

  // Objects/Inspector: wide enough for a name/icon column and field labels, capped so a narrow window
  // still leaves the center scene view usable.
  constexpr float leftPanelWidth = 280.0f;
  constexpr float rightPanelWidth = 360.0f;
  constexpr float maxSideFraction = 0.3f;

  // Assets/Project Errors: enough height for a row of thumbnails or a few log lines.
  constexpr float bottomPanelHeight = 240.0f;
  constexpr float maxBottomFraction = 0.35f;

  const float leftWidth = std::min(leftPanelWidth, viewportSize.x * maxSideFraction);
  const float rightWidth = std::min(rightPanelWidth, viewportSize.x * maxSideFraction);
  const float bottomHeight = std::min(bottomPanelHeight, viewportSize.y * maxBottomFraction);

  // The default layout sizes Scene Status for one row of controls: title bar + padding + one control row
  // fits it exactly at any font size or DPI, with no scrollbar. A narrow window wraps the controls onto more
  // rows and displaySceneStatus raises the floor to fit them.
  constexpr int sceneStatusRows = 1;
  const float topHeight = toolbarPanelHeight(sceneStatusRows, ImGui::GetFrameHeight(),
                                             ImGui::GetStyle().WindowPadding.y, ImGui::GetFrameHeightWithSpacing());

  const auto gui = m_renderer->getImGuiInstance();

  // DockBuilderSplitNode cuts each dock from whatever remains of the node (left, then right of that,
  // then top, then bottom of what's left), so later percents must be relative to the reduced node, not
  // the full viewport.
  const float leftPercent = leftWidth / viewportSize.x;
  const float rightPercent = rightWidth / (viewportSize.x - leftWidth);
  const float topPercent = topHeight / viewportSize.y;
  const float bottomPercent = bottomHeight / (viewportSize.y - topHeight);

  gui->setTopDockPercent(topPercent);
  gui->setBottomDockPercent(bottomPercent);

  gui->setLeftDockPercent(leftPercent);
  gui->setRightDockPercent(rightPercent);

  return true;
}
