#ifndef PROJECTIDENTITYCHECK_H
#define PROJECTIDENTITYCHECK_H

#include <cstdint>

// Decides, on the first snapshot of a connection, whether the project the server holds is the one this
// editor started with. A spawned server was handed the editor's own --project, so it is trusted; with
// --host the server may have been started on something else. Fingerprints come from
// fingerprintProjectFile: zero means no project file.
class ProjectIdentityCheck {
public:
  void setOwnProject(const uint64_t fingerprint, const bool serverWasSpawned)
  {
    m_ownFingerprint = fingerprint;
    m_serverWasSpawned = serverWasSpawned;
  }

  // A new connection is checked again.
  void reset()
  {
    m_checked = false;
  }

  // Called with each snapshot's reported fingerprint. True once per connection, when the server holds a
  // different project than the editor's own file, so the editor's save target does not describe it.
  [[nodiscard]] bool observeSnapshot(const uint64_t serverFingerprint)
  {
    if (m_checked)
    {
      return false;
    }

    m_checked = true;

    if (m_serverWasSpawned || m_ownFingerprint == 0)
    {
      return false;
    }

    return serverFingerprint != m_ownFingerprint;
  }

private:
  uint64_t m_ownFingerprint = 0;
  bool m_serverWasSpawned = false;
  bool m_checked = false;
};

#endif //PROJECTIDENTITYCHECK_H
