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

  const auto depthBefore = m_editHistory.undoDepth();
  undo();

  if (m_editHistory.undoDepth() != depthBefore)
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

  const auto depthBefore = m_editHistory.redoDepth();
  redo();

  if (m_editHistory.redoDepth() != depthBefore)
  {
    beginUndoRedoPending();
  }
}

bool EditorApp::undoRedoRequestBlocked()
{
  if (!m_undoRedoPending)
  {
    return false;
  }

  if (std::chrono::steady_clock::now() - m_undoRedoPendingSince >= undoRedoPendingTimeout)
  {
    m_undoRedoPending = false;
    return false;
  }

  return true;
}

void EditorApp::beginUndoRedoPending()
{
  m_undoRedoPending = true;
  m_undoRedoPendingSince = std::chrono::steady_clock::now();
}

void EditorApp::clearUndoRedoPending()
{
  m_undoRedoPending = false;
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
    ? m_editHistory.nextUndoLabel(*objectManager, m_assetRegistry.get())
    : std::nullopt;
  const auto redoLabel = objectManager
    ? m_editHistory.nextRedoLabel(*objectManager, m_assetRegistry.get())
    : std::nullopt;

  // Not blocking on undoRedoRequestBlocked() here would leave a stale-looking enabled item during the
  // gate's window; calling it also lets the gate clear itself once its timeout has passed, even if
  // neither handler below got a chance to.
  const bool requestInFlight = undoRedoRequestBlocked();

  const std::string undoText = "Undo" + (undoLabel ? " " + *undoLabel : std::string());
  const std::string redoText = "Redo" + (redoLabel ? " " + *redoLabel : std::string());
  const std::string undoShortcut = shortcutFor(*m_keybindTable, EditorAction::undo);
  const std::string redoShortcut = shortcutFor(*m_keybindTable, EditorAction::redo);

  // nullptr rather than "" for an unbound action - an empty shortcut column still reserves the same
  // layout space "Ctrl+Z" would, which reads as a blank rather than nothing.
  const char* undoShortcutText = undoShortcut.empty() ? nullptr : undoShortcut.c_str();
  const char* redoShortcutText = redoShortcut.empty() ? nullptr : redoShortcut.c_str();

  ImGui::BeginDisabled(!undoLabel || !m_serverEditable || requestInFlight);
  if (ImGui::MenuItem(undoText.c_str(), undoShortcutText))
  {
    requestUndo();
  }
  ImGui::EndDisabled();

  ImGui::BeginDisabled(!redoLabel || !m_serverEditable || requestInFlight);
  if (ImGui::MenuItem(redoText.c_str(), redoShortcutText))
  {
    requestRedo();
  }
  ImGui::EndDisabled();

  ImGui::EndMenu();
}
