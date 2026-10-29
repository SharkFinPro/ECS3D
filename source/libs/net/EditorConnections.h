#ifndef EDITORCONNECTIONS_H
#define EDITORCONNECTIONS_H

#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace net {

// The server's per-connection authorization bookkeeping: which connections the transport granted
// Role::editor at the handshake, and the ids of dropped connections awaiting the tick thread. Touched
// from the socket threads (authorize, enqueueDisconnect) and the tick thread (the rest).
class EditorConnections {
public:
  // Records connId as an editor when role is Role::editor; any other role is a no-op.
  void authorize(int32_t connId, uint8_t role);

  [[nodiscard]] bool isEditor(int32_t connId) const;

  // A copy of the current editor ids, taken under the lock so the caller can use the transport after
  // releasing it. Order is unspecified.
  [[nodiscard]] std::vector<int32_t> editorIds() const;

  // Buffers a dropped connection. Does not revoke editor status: a message the connection sent just
  // before dropping may still be undrained, and must keep its authorization until revokeEditor().
  void enqueueDisconnect(int32_t connId);

  // Returns the ids buffered since the last call and clears the buffer. Editor status is untouched, so the
  // caller can drain those connections' messages first and revoke afterwards.
  [[nodiscard]] std::vector<int32_t> takeDisconnected();

  // Revokes connId's editor status; a no-op for one that is not an editor.
  void revokeEditor(int32_t connId);

private:
  std::mutex m_disconnectMutex;
  std::vector<int32_t> m_disconnected;

  mutable std::mutex m_editorMutex;
  std::unordered_set<int32_t> m_editorConnections;
};

}

#endif //EDITORCONNECTIONS_H
