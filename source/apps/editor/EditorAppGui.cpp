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
#include <SettingsStore.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/imGui/ImGuiInstance.h>
#include <objects/Object.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <optional>

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
  m_inspectorPanel->displayGui(objectManager, activeSceneUUID);

  // Draws nothing while closed. Preferences are local, so it needs no scene and no server.
  m_settingsPanel->displayGui();

  m_consolePanel->displayGui();

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

    ImGui::EndMenu();
  }
}

void EditorApp::updateDockSpace() const
{
  static bool dockPercentsSetup = false;
  static bool dockLocationsSetup = false;

  if (!dockLocationsSetup && dockPercentsSetup)
  {
    applyDockLocations();

    dockLocationsSetup = true;
  }

  if (!dockPercentsSetup)
  {
    dockPercentsSetup = applyDockPercents();
  }
}

void EditorApp::applyDockLocations() const
{
  const auto gui = m_renderer->getImGuiInstance();

  gui->dockCenter(m_sceneViewName.c_str());

  gui->dockLeft("Objects");

  gui->dockRight("Inspector");

  gui->dockTop("Scene Status");

  gui->dockBottom("Assets");
  gui->dockBottom("Project Errors");
  gui->dockBottom("Console");

  // Unscaled pixels; the engine applies content scale. Each floor is on one axis so the splitter can still
  // shrink the other, and tabs sharing a dock node take the largest floor among them. In a window small
  // enough that the default sizes fall below these, the floors win over the default layout.
  const ImVec2 objectsMinimumSize{260.0f, 0.0f};
  const ImVec2 inspectorMinimumSize{310.0f, 0.0f};
  const ImVec2 sceneStatusMinimumSize{0.0f, 88.0f};
  const ImVec2 assetsMinimumSize{0.0f, 200.0f};
  const ImVec2 projectErrorsMinimumSize{0.0f, 90.0f};
  const ImVec2 consoleMinimumSize{0.0f, 120.0f};

  gui->setDockedWindowMinimumSize("Objects", objectsMinimumSize);
  gui->setDockedWindowMinimumSize("Inspector", inspectorMinimumSize);
  gui->setDockedWindowMinimumSize("Scene Status", sceneStatusMinimumSize);
  gui->setDockedWindowMinimumSize("Assets", assetsMinimumSize);
  gui->setDockedWindowMinimumSize("Project Errors", projectErrorsMinimumSize);
  gui->setDockedWindowMinimumSize("Console", consoleMinimumSize);
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

  // Scene Status draws its controls on a single row (see displaySceneStatus): title bar + padding + one
  // control row fits it exactly at any font size or DPI, with no scrollbar.
  constexpr int sceneStatusRows = 1;
  const float topHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f
                         + ImGui::GetFrameHeightWithSpacing() * sceneStatusRows;

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
