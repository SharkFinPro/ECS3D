#include "EditorApp.h"
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <assets/AssetRegistry.h>
#include <Keybinds.h>
#include <imgui.h>
#include <optional>
#include <string>

namespace {
  // The keybind table only carries a chord, not display text - MenuItem's shortcut column wants the same
  // "Ctrl+Shift+Z" spelling Settings shows for a rebind, or nothing when the action is unbound.
  std::string shortcutFor(const KeybindTable& table, const EditorAction action)
  {
    const auto chord = table.binding(action);
    return chord ? formatChord(*chord) : std::string();
  }
}

void EditorApp::requestUndo()
{
  if (undoRedoRequestBlocked())
  {
    return;
  }

  const auto redoDepthBefore = m_playHistory.current().redoDepth();
  undo();

  if (gainedOneEntry(redoDepthBefore, m_playHistory.current().redoDepth()))
  {
    beginUndoRedoPending();
  }
}

void EditorApp::requestRedo()
{
  if (undoRedoRequestBlocked())
  {
    return;
  }

  const auto undoDepthBefore = m_playHistory.current().undoDepth();
  redo();

  if (gainedOneEntry(undoDepthBefore, m_playHistory.current().undoDepth()))
  {
    beginUndoRedoPending();
  }
}

bool EditorApp::undoRedoRequestBlocked()
{
  return m_undoRedoGate.blocked();
}

void EditorApp::beginUndoRedoPending()
{
  m_undoRedoGate.begin();
}

void EditorApp::clearUndoRedoPending()
{
  m_undoRedoGate.clear();
}

void EditorApp::clearEditHistory()
{
  m_playHistory.clear();
  clearUndoRedoPending();
}

void EditorApp::displayUndoRedoMenuItem(const bool isUndo, const std::optional<std::string>& label,
                                        const bool enabled)
{
  const char* verb = isUndo ? "Undo" : "Redo";
  const std::string text = std::string(verb) + (label ? " " + *label : std::string());

  const std::string shortcut = shortcutFor(*m_keybindTable, isUndo ? EditorAction::undo : EditorAction::redo);
  // nullptr rather than "" for an unbound action - an empty shortcut column still reserves the same
  // layout space "Ctrl+Z" would, which reads as a blank rather than nothing.
  const char* shortcutText = shortcut.empty() ? nullptr : shortcut.c_str();

  ImGui::BeginDisabled(!enabled);
  if (ImGui::MenuItem(text.c_str(), shortcutText))
  {
    if (isUndo)
    {
      requestUndo();
    }
    else
    {
      requestRedo();
    }
  }
  ImGui::EndDisabled();
}

void EditorApp::displayEditMenu()
{
  if (!ImGui::BeginMenu("Edit"))
  {
    return;
  }

  const auto scene = m_sceneManager->getCurrentScene();
  const auto* objectManager = scene ? scene->getObjectManager().get() : nullptr;

  const auto undoLabel = objectManager
    ? m_playHistory.current().nextUndoLabel(*objectManager, m_assetRegistry.get())
    : std::nullopt;
  const auto redoLabel = objectManager
    ? m_playHistory.current().nextRedoLabel(*objectManager, m_assetRegistry.get())
    : std::nullopt;

  // Not blocking on undoRedoRequestBlocked() here would leave a stale-looking enabled item during the
  // gate's window; calling it also lets the gate clear itself once its timeout has passed, even if
  // neither handler below got a chance to.
  const bool requestInFlight = undoRedoRequestBlocked();

  displayUndoRedoMenuItem(true, undoLabel, canActOnHistoryItem(undoLabel, m_serverEditable, requestInFlight));
  displayUndoRedoMenuItem(false, redoLabel, canActOnHistoryItem(redoLabel, m_serverEditable, requestInFlight));

  ImGui::EndMenu();
}
