#ifndef EDITHISTORY_H
#define EDITHISTORY_H

#include "EditCommand.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

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

  // Groups several commands into one undo/redo entry (a multi-object delete or duplicate): one Ctrl+Z
  // reverses the whole group. Empty is ignored; one command behaves exactly like record(); otherwise every
  // command must have payloadForm() == PayloadForm::sceneEdit (a group is sent as one batch sceneEdit) or
  // this throws std::invalid_argument. Same maxDepth/redo-clearing semantics as record(), counting entries
  // rather than commands.
  void recordBatch(std::vector<EditCommand> commands);

  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;

  // The kind of command undo()/redo() would act on next, without popping either stack - lets a caller
  // that only knows how to handle some kinds (see EditorApp::undo()/redo()) decide whether to attempt it
  // at all, and leave an entry it does not yet handle sitting on top rather than have undo()/redo() treat
  // it as a validation conflict and drop it. nullopt when that stack is empty. For a grouped entry
  // (recordBatch), this is the kind of the group's last command.
  [[nodiscard]] std::optional<CommandKind> nextUndoKind() const;
  [[nodiscard]] std::optional<CommandKind> nextRedoKind() const;

  // Whether undo()/redo() would actually produce a payload for the top of that stack, without popping
  // either stack - false for an empty stack as well as for a non-reversible entry (every command in a
  // grouped entry, for a group), so a caller can gate an attempt on this alone rather than also checking
  // canUndo()/canRedo().
  [[nodiscard]] bool nextUndoIsReversible() const;
  [[nodiscard]] bool nextRedoIsReversible() const;

  // Validates the top of the undo stack against the live scene/registry and, on success, moves it to the
  // redo stack and returns the reverse payload to send. On refusal, drops that entry and everything older
  // still on the undo stack (entries already on the redo stack are untouched - they are newer, already-
  // undone commands, not part of this chain) and reports which uuid conflicted.
  //
  // A grouped entry (recordBatch) is validated and reversed as one unit: a scratch copy of objectManager
  // simulates the group's commands in reverse order (each one's validateForUndo, then its buildUndoJSON
  // applied to the scratch so the next command's validation sees what the one before it would have
  // undone), and the payload sent is one replication::buildBatch of the collected per-command undo ops -
  // so one Ctrl+Z reverses the whole group in one sceneEdit. A single-command entry skips the scratch copy
  // and batch wrapping entirely, so its payload is unchanged from before grouping existed.
  [[nodiscard]] HistoryOutcome undo(const ObjectManager& objectManager,
                                    const AssetRegistry* assetRegistry = nullptr);

  // Symmetric with undo(): validates the top of the redo stack, moves it back to the undo stack on
  // success, or drops it and everything older still on the redo stack on refusal. A grouped entry replays
  // its commands forward (validateForRedo/buildRedoJSON) the same way undo() replays them in reverse.
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

  // Human-readable description of the command undo()/redo() would act on next ("Rename Cube"), for an
  // Edit menu that names the next action instead of showing a bare "Undo"/"Redo" - see
  // EditCommand::describeForMenu. nullopt when that stack is empty. A grouped entry (recordBatch) is
  // "Delete N Objects"/"Duplicate N Objects" when every command in it shares that kind, else "N Edits".
  [[nodiscard]] std::optional<std::string> nextUndoLabel(const ObjectManager& objectManager,
                                                         const AssetRegistry* assetRegistry = nullptr) const;
  [[nodiscard]] std::optional<std::string> nextRedoLabel(const ObjectManager& objectManager,
                                                         const AssetRegistry* assetRegistry = nullptr) const;

  // Stack sizes, for a caller that needs to detect whether undo()/redo() actually popped an entry (sent a
  // request) without reading their bodies - see EditorApp's in-flight gate on repeated undo/redo requests.
  [[nodiscard]] std::size_t undoDepth() const;
  [[nodiscard]] std::size_t redoDepth() const;

private:
  // Each entry is one undo/redo step: one command for an ordinary edit, several for a group recorded
  // through recordBatch (a multi-object delete or duplicate), reversed as one unit.
  std::deque<std::vector<EditCommand>> m_undoStack;
  std::deque<std::vector<EditCommand>> m_redoStack;
};

}

#endif //EDITHISTORY_H
