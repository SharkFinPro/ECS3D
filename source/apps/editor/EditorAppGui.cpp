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
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/imGui/ImGuiInstance.h>
#include <objects/Object.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <optional>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cmath>
#include <imgui_internal.h>

void EditorApp::updateGui()
{
  // Drawn regardless of the GUI toggle below: a window-close request can arrive while the GUI is
  // hidden, and the prompt must not be hideable out from under the user.
  m_saveUI->displayUnsavedChangesModal();

  if (!m_shouldDisplayGui)
  {
    m_settingsPanel->abandonKeyCapture();

    return;
  }

  // Propagate the server's editability so the panels disable their mutating affordances (and show a
  // read-only cue) on a non-edit server.
  m_objectGUIManager->setEditable(m_serverEditable);
  m_objectGUIManager->setReadOnlyNotice(editorConnectionPanelNotice(m_connectionTracker.state()));
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

    // A persistent badge, right-aligned, whenever the editor cannot edit; the text names why.
    if (const char* badge = editorConnectionBadge(m_connectionTracker.state()); badge && !m_serverEditable)
    {
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
// The Scene Status toolbar is sized to its content by displaySceneStatus, not by a share of the window.
bool isSceneStatusToolbar(const ImGuiDockNode* node)
{
  const bool holdsOnlyTheToolbar = node->Windows.Size == 1 && std::strcmp(node->Windows[0]->Name, "Scene Status") == 0;

  return isContentSizedToolbarNode(node, holdsOnlyTheToolbar);
}

// A splitter or tab drag in the dockspace owns the layout until it ends.
bool dockspaceHasActiveDrag(const ImGuiDockNode* root)
{
  const ImGuiContext& context = *ImGui::GetCurrentContext();

  return context.ActiveId != 0 && root->HostWindow != nullptr && context.ActiveIdWindow == root->HostWindow;
}

void rescaleLayout(const DockProportions& proportions, const ImVec2 extent, const DockTreeSides& tree)
{
  for (const auto& [id, axis, size] : proportions.fit(extent.x, extent.y))
  {
    const auto node = std::ranges::find(tree.existing, id);

    if (node != tree.existing.end())
    {
      tree.all[node - tree.existing.begin()]->SizeRef[axis] = size;
    }
  }
}

void rememberLayout(DockProportions& proportions, const bool settling, const ImVec2 extent,
                    const DockTreeSides& tree)
{
  proportions.rememberChanges(tree.sides, settling ? std::vector<DockSideSize>{} : tree.sides, tree.existing,
                              extent.x, extent.y);
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

    // The engine rebuilds the default tree on the frame after the percents change and keeps the node ids, so
    // the proportions are taken again from the rebuilt tree.
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

  updateDockMinimumSizesForViewport();

  DockTreeSides tree;
  collectDockSides(root, tree.nodes, tree.sides, [&tree](ImGuiDockNode* node)
  {
    const bool contentSized = isSceneStatusToolbar(node);

    if (contentSized)
    {
      tree.contentSized.push_back(node);
    }

    return contentSized;
  });

  collectAllDockNodes(root, tree.all);

  for (const ImGuiDockNode* node : tree.all)
  {
    tree.existing.push_back(static_cast<std::uint32_t>(node->ID));
  }

  const bool settling = m_dockSettleFrames > 0;
  const bool layoutChanged = settling || !m_dockProportions.tracks(tree.sides);
  const bool dragging = dockspaceHasActiveDrag(root);

  m_dockSettleFrames = std::max(m_dockSettleFrames - 1, 0);

  const DockLayoutStep step = m_dockResizeGate.update(root->Size.x, root->Size.y, dragging, layoutChanged);

  runDockStep(step, settling, root->Size, tree);

  if (!dragging)
  {
    sizeSceneStatusNodes(tree.contentSized);
  }
}

void EditorApp::runDockStep(const DockLayoutStep step, const bool settling, const ImVec2 extent,
                            const DockTreeSides& tree)
{
  switch (step)
  {
  case DockLayoutStep::remember:
    rememberLayout(m_dockProportions, settling, extent, tree);
    break;

  case DockLayoutStep::rememberAndRescale:
    rememberLayout(m_dockProportions, settling, extent, tree);

    if (!settling)
    {
      rescaleLayout(m_dockProportions, extent, tree);
    }
    break;

  case DockLayoutStep::rescale:
    rescaleLayout(m_dockProportions, extent, tree);
    break;

  case DockLayoutStep::beginDrag:
    m_dockDragStart = tree.sides;
    break;

  case DockLayoutStep::endDrag:
    m_dockProportions.rememberChanges(tree.sides, m_dockDragStart, tree.existing, extent.x, extent.y);
    break;

  case DockLayoutStep::none:
    break;
  }
}

void EditorApp::sizeSceneStatusNodes(const std::vector<ImGuiDockNode*>& nodes)
{
  const float scale = m_renderer->getWindow()->getContentScale();
  const float previous = std::ceil(m_sceneStatusAppliedFloor * scale);
  const float height = std::ceil(m_sceneStatusFloor * scale);

  m_sceneStatusAppliedFloor = m_sceneStatusFloor;

  // A wider toolbar wraps into fewer rows, so a node still sitting at the old floor drops to the new content
  // height instead of keeping the larger one an earlier, narrower window left in its SizeRef. A node the user
  // sized differently, and a taller floor (the engine enforces it), are left alone.
  if (previous <= 0.0f || height >= previous)
  {
    return;
  }

  for (ImGuiDockNode* node : nodes)
  {
    if (std::fabs(node->SizeRef.y - previous) <= dockEditTolerance)
    {
      node->SizeRef.y = height;
    }
  }
}

void EditorApp::updateDockMinimumSizesForViewport()
{
  const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
  const float contentScale = m_renderer->getWindow()->getContentScale();

  if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
  {
    return;
  }

  const std::array<float, 3> key{viewportSize.x, viewportSize.y, contentScale};

  if (key != m_dockFloorKey)
  {
    m_dockFloorKey = key;
    applyDockPanelMinimumSizes();
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
  const float contentScale = std::max(m_renderer->getWindow()->getContentScale(), 0.01f);
  const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
  const float width = viewportSize.x / contentScale;
  const float height = viewportSize.y / contentScale;

  const auto sideFloor = [&](const float base)
  {
    return ImVec2{dockFloorForExtent(base, width, dockSideFloorFraction), 0.0f};
  };

  const auto bottomFloor = [&](const float base)
  {
    return ImVec2{0.0f, dockFloorForExtent(base, height, dockBottomFloorFraction)};
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
