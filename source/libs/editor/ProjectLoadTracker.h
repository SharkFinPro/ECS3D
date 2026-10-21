#ifndef PROJECTLOADTRACKER_H
#define PROJECTLOADTRACKER_H

#include <uuid.h>
#include <optional>

// Tells another editor's loadProject apart from an ordinary re-snapshot, by the project load id each
// snapshot ends with (ProjectPacker). Known limit: if the server refuses this editor's load, its id never
// comes back and the tracker keeps waiting until this editor loads again or reconnects.
class ProjectLoadTracker {
public:
  // This editor is about to send loadProject carrying id.
  void expectOwnLoad(const uuids::uuid& id)
  {
    m_pendingOwn = id;
  }

  // A new connection starts a new baseline.
  void reset()
  {
    m_current.reset();
    m_pendingOwn.reset();
  }

  // Called with each snapshot's load id. True when the snapshot belongs to a project load this editor did
  // not make, so its save target no longer describes what the server holds.
  [[nodiscard]] bool observeSnapshot(const uuids::uuid& id)
  {
    if (m_pendingOwn)
    {
      if (id == *m_pendingOwn)
      {
        m_current = id;
        m_pendingOwn.reset();
      }

      return false;
    }

    if (!m_current)
    {
      m_current = id;
      return false;
    }

    if (id == *m_current)
    {
      return false;
    }

    m_current = id;
    return true;
  }

private:
  std::optional<uuids::uuid> m_current;
  std::optional<uuids::uuid> m_pendingOwn;
};

#endif //PROJECTLOADTRACKER_H
