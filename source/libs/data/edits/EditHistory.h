#ifndef EDITHISTORY_H
#define EDITHISTORY_H

#include "EditCommand.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>
#include <cstddef>
#include <deque>
#include <optional>

class ObjectManager;
class AssetRegistry;

// The editor's undo/redo stacks. Deliberately headless: no ImGui, no networking, no EditorApp. A caller
// (the editor, wiring this up in a later change) hands undo()/redo() the live ObjectManager/AssetRegistry
// to validate against, gets back either a payload to send or a refusal naming what conflicted, and sends
// the payload itself - this library never touches the network.
//
// Undo is a new edit: the payload undo() hands back is an ordinary reverse edit, sent and applied through
// the normal replication path like anything else the editor does. Validation happens here, at the moment
// of undo/redo, against the live state - not on every snapshot, which would invalidate the whole stack on
// every structural edit even in single-user editing (see EditCommand.h's validateForUndo/validateForRedo).
//
// Exception contract: undo()/redo() do not catch anything thrown by validation or by the build* calls -
// notably nlohmann::json::parse on a stored before/after blob. A stored blob only becomes unparseable
// through data corruption (a hand-edited save, a truncated write, a bug elsewhere), which is a defect to
// surface loudly, not a routine refusal like a missing uuid - swallowing it here would hide the bug behind
// a generic "not undoable" the next person has no way to diagnose. It is the caller's (the editor's)
// responsibility to keep such a throw from reaching its frame loop.
namespace edits {

// Why undo()/redo() did not hand back a payload to send, in the same spirit as Replication.h's
// SceneEditResult/ComponentEditResult: the caller needs to say something different to the user for each.
enum class HistoryResult {
  applied,       // a payload was built; send it
  historyEmpty,  // nothing to undo/redo - not an error
  notUndoable,   // this command's kind has no faithful reverse with the builders that exist
  targetMissing, // the object/asset/component this command targets is gone
  targetChanged  // it exists, but has drifted from what this command expects
};

struct HistoryOutcome {
  HistoryResult result = HistoryResult::historyEmpty;
  std::optional<uuids::uuid> conflict; // set for notUndoable/targetMissing/targetChanged

  // Exactly one is set when result == applied, matching the command's payloadForm().
  std::optional<nlohmann::json> jsonPayload;
  std::optional<net::Message> messagePayload;

  [[nodiscard]] bool ok() const
  {
    return result == HistoryResult::applied;
  }
};

class EditHistory {
public:
  // Bounds memory for a long editing session. 100 is generous for the back-and-forth a single session
  // realistically produces while still being a hard, named limit instead of unbounded growth.
  static constexpr std::size_t maxDepth = 100;

  // Pushes onto the undo stack and clears the redo stack - any new command invalidates whatever redo
  // path existed, since redoing it now would apply on top of a scene that has since moved on. Drops the
  // oldest entry (front) rather than refusing to record, once the stack exceeds maxDepth.
  void record(EditCommand command);

  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;

  // Validates the top of the undo stack against the live scene/registry and, on success, moves it to the
  // redo stack and returns the reverse payload to send. On refusal, drops that entry and everything older
  // still on the undo stack (entries already on the redo stack are untouched - they are newer, already-
  // undone commands, not part of this chain) and reports which uuid conflicted.
  [[nodiscard]] HistoryOutcome undo(const ObjectManager& objectManager,
                                    const AssetRegistry* assetRegistry = nullptr);

  // Symmetric with undo(): validates the top of the redo stack, moves it back to the undo stack on
  // success, or drops it and everything older still on the redo stack on refusal.
  [[nodiscard]] HistoryOutcome redo(const ObjectManager& objectManager,
                                    const AssetRegistry* assetRegistry = nullptr);

  // The server rejected the edit that undo()/redo() just produced, rather than the pre-flight validation
  // catching a divergence before anything was sent. Surfaced the same way as a validation failure: drop
  // that one entry. It is sitting on the opposite stack from the one it came from, since undo()/redo()
  // already moved it there optimistically when it handed back the payload.
  void reportUndoRejected();
  void reportRedoRejected();

  // Loading a project, switching scenes, reconnecting, or starting/stopping play all invalidate both
  // directions at once (see EditCommand.h's module comment) - stopping play restores the authored scene,
  // which neither stack's recorded state has any relation to any more.
  void clear();

private:
  std::deque<EditCommand> m_undoStack;
  std::deque<EditCommand> m_redoStack;
};

}

#endif //EDITHISTORY_H
