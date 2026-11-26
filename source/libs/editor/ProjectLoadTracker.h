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

  // True from expectOwnLoad until the snapshot carrying that load arrives: the server has not yet applied it.
  [[nodiscard]] bool awaitingOwnLoad() const
  {
    return m_pendingOwn.has_value();
  }

  // Whether a snapshot carrying id reflects what the server holds now. While this editor's own load is awaited, any
  // other snapshot was sent before the server applied it and still lists the old project, so what it lacks says
  // nothing about the project being loaded. Asked before observeSnapshot, which ends the wait.
  [[nodiscard]] bool snapshotIsCurrent(const uuids::uuid& id) const
  {
    return !m_pendingOwn || id == *m_pendingOwn;
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
