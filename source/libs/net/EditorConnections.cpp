#include "EditorConnections.h"
#include <Protocol.h>
#include <utility>

namespace net {

void EditorConnections::authorize(const int32_t connId, const uint8_t role)
{
  if (static_cast<Role>(role) != Role::editor)
  {
    return;
  }

  std::lock_guard lock(m_editorMutex);
  m_editorConnections.insert(connId);
}

bool EditorConnections::isEditor(const int32_t connId) const
{
  std::lock_guard lock(m_editorMutex);
  return m_editorConnections.contains(connId);
}

std::vector<int32_t> EditorConnections::editorIds() const
{
  std::lock_guard lock(m_editorMutex);
  return { m_editorConnections.begin(), m_editorConnections.end() };
}

void EditorConnections::enqueueDisconnect(const int32_t connId)
{
  std::lock_guard lock(m_disconnectMutex);
  m_disconnected.push_back(connId);
}

std::vector<int32_t> EditorConnections::takeDisconnected()
{
  std::vector<int32_t> disconnected;
  {
    std::lock_guard lock(m_disconnectMutex);
    disconnected = std::exchange(m_disconnected, {});
  }

  // Connection ids are never reused (both transport backends assign them via Interlocked.Increment),
  // so a deferred erase cannot let a new connection inherit a stale editor entry.
  {
    std::lock_guard lock(m_editorMutex);
    for (const auto connId : disconnected)
    {
      m_editorConnections.erase(connId);
    }
  }

  return disconnected;
}

}
