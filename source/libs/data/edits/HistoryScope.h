#ifndef HISTORYSCOPE_H
#define HISTORYSCOPE_H

#include <optional>
#include <uuid.h>

namespace edits {

// Remembers which scene the recorded undo history belongs to, so a snapshot that swaps in a different
// active scene (another editor's load or scene switch) can be told apart from an ordinary rebroadcast.
class HistoryScope {
public:
  // The history was just cleared for this scene (nullopt: not known yet).
  void reset(std::optional<uuids::uuid> sceneUUID);

  // Call with the active scene after a snapshot is applied. Returns true, and adopts the new scene, when
  // it differs from the one remembered.
  [[nodiscard]] bool observe(std::optional<uuids::uuid> sceneUUID);

private:
  std::optional<uuids::uuid> m_sceneUUID;
};

}

#endif
