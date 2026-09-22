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

  // Whether a request actually sent something, read from the OPPOSITE stack's depth (redo's for undo(),
  // undo's for redo()) rather than the one undo()/redo() popped from. A refusal (notUndoable/
  // targetMissing/targetChanged) clears the popped-from stack down to 0 exactly the same way a legitimate
  // pop shrinks it by one, so comparing that stack alone cannot tell a refusal from a send - and either
  // would otherwise sit the gate for 500ms over nothing, swallowing a legitimate follow-up undo. The
  // opposite stack is untouched by a refusal (see EditHistory::undo()/redo() to the closing brace) and only
  // ever gains exactly one entry on success: both push straight onto it, bypassing record() - the only
  // place maxDepth trimming happens - so there is no trim to make a +1 read as unchanged. That keeps this a
  // depth comparison rather than needing a dedicated counter on EditHistory.
  bool gainedOneEntry(const std::size_t before, const std::size_t after)
  {
    return after == before + 1;
  }
}

void EditorApp::requestUndo()
{
  if (undoRedoRequestBlocked())
  {
    return;
  }

  const auto redoDepthBefore = m_editHistory.redoDepth();
  undo();

  if (gainedOneEntry(redoDepthBefore, m_editHistory.redoDepth()))
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

  const auto undoDepthBefore = m_editHistory.undoDepth();
  redo();

  if (gainedOneEntry(undoDepthBefore, m_editHistory.undoDepth()))
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

bool EditorApp::canActOnHistoryItem(const std::optional<std::string>& label, const bool requestInFlight) const
{
  return label.has_value() && m_serverEditable && !requestInFlight;
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
    ? m_editHistory.nextUndoLabel(*objectManager, m_assetRegistry.get())
    : std::nullopt;
  const auto redoLabel = objectManager
    ? m_editHistory.nextRedoLabel(*objectManager, m_assetRegistry.get())
    : std::nullopt;

  // Not blocking on undoRedoRequestBlocked() here would leave a stale-looking enabled item during the
  // gate's window; calling it also lets the gate clear itself once its timeout has passed, even if
  // neither handler below got a chance to.
  const bool requestInFlight = undoRedoRequestBlocked();

  displayUndoRedoMenuItem(true, undoLabel, canActOnHistoryItem(undoLabel, requestInFlight));
  displayUndoRedoMenuItem(false, redoLabel, canActOnHistoryItem(redoLabel, requestInFlight));

  ImGui::EndMenu();
}
