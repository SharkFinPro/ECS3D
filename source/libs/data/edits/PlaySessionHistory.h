#ifndef PLAYSESSIONHISTORY_H
#define PLAYSESSIONHISTORY_H

#include "EditHistory.h"
#include "scenes/SceneManager.h"
#include <deque>
#include <optional>

namespace edits {

// Keeps the authored undo history apart from a play session's. The server applies edits in the order they
// were sent, so the swap is made when Start/Stop is requested, not when the status comes back; a status the
// server reports before it has seen our request is stale and ignored. A start/stop made by another editor
// is followed from the reported status alone. Accepted limitation: a Start the server never acts on (no
// current scene) leaves its expectation queued, so edits recorded meanwhile stay on the play history until a
// Stop or clear().
class PlaySessionHistory {
public:
  // What undo/redo act on: the authored history, or the play session's while one runs.
  [[nodiscard]] EditHistory& current();
  [[nodiscard]] const EditHistory& current() const;

  // Call where a start/stop is sent. Return whether the histories were swapped.
  bool requestStart();
  bool requestStop();

  // Call with each sceneStatus the server reports. Returns whether the histories were swapped.
  bool observeStatus(SceneStatus status);

  // Drops both histories and any pending expectations; the last reported status is kept.
  void clear();

  // clear() plus forgetting the reported status, for a new server whose first report is not a transition.
  void reset();

  [[nodiscard]] bool hasStash() const;

private:
  EditHistory m_current;
  std::optional<EditHistory> m_authored;
  // nullopt until the first report, so that one is not read as a start/stop transition.
  std::optional<SceneStatus> m_reported;

  // Outstanding requests, oldest first: stopped for a Stop, running (any non-stopped status) for a Start.
  // A status confirms only the front, so a stale one cannot confirm a later request.
  std::deque<SceneStatus> m_expected;
};

}

#endif //PLAYSESSIONHISTORY_H
