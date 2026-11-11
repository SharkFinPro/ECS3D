#ifndef EDITORCONNECTIONSTATE_H
#define EDITORCONNECTIONSTATE_H

// Why the editor can or cannot edit. Only `connected` allows edits; the other states all read as "read-only" in
// the UI but name different causes, so the text points at the right fix.
enum class EditorConnectionState
{
  connected,
  readOnlyServer,
  connectionLost,
  editTokenRejected
};

[[nodiscard]] constexpr EditorConnectionState editorConnectionForEditStatus(const bool serverEditable) noexcept
{
  return serverEditable ? EditorConnectionState::connected : EditorConnectionState::readOnlyServer;
}

// A server that accepts the editor's join replies with an edit status and a snapshot, so an external server that
// drops the editor before sending anything refused it at the handshake. The transport reports no distinct close
// reason, so a server that refused the editor for another reason lands here too. A server the editor spawned
// uses a token the editor generated, so a drop from that one is never a rejection.
[[nodiscard]] constexpr EditorConnectionState editorConnectionAfterLoss(const bool receivedFromServer,
                                                                        const bool externalServer) noexcept
{
  if (!receivedFromServer && externalServer)
  {
    return EditorConnectionState::editTokenRejected;
  }

  return EditorConnectionState::connectionLost;
}

[[nodiscard]] constexpr const char* editorConnectionBadge(const EditorConnectionState state) noexcept
{
  switch (state)
  {
    case EditorConnectionState::connected:
      return nullptr;
    case EditorConnectionState::readOnlyServer:
      return "READ-ONLY (server not in edit mode)";
    case EditorConnectionState::connectionLost:
      return "Disconnected from server";
    case EditorConnectionState::editTokenRejected:
      return "Edit token rejected (or the server refused the connection)";
  }

  return nullptr;
}

[[nodiscard]] constexpr const char* editorConnectionPanelNotice(const EditorConnectionState state) noexcept
{
  switch (state)
  {
    case EditorConnectionState::connected:
      return nullptr;
    case EditorConnectionState::readOnlyServer:
      return "Read-only - server is not in edit mode";
    case EditorConnectionState::connectionLost:
      return "Read-only - disconnected from the server";
    case EditorConnectionState::editTokenRejected:
      return "Read-only - edit token rejected by the server";
  }

  return nullptr;
}

[[nodiscard]] constexpr const char* editorConnectionLogMessage(const EditorConnectionState state) noexcept
{
  switch (state)
  {
    case EditorConnectionState::editTokenRejected:
      return "The server closed the connection before replying to the editor: the edit token was wrong or missing, or the "
             "server refused the editor. Check the --token value.";
    default:
      return "Connection to the server was lost. Save your work and restart the editor.";
  }
}

// The editor's connection state as the app's network loop drives it. A loss is final until the next connect: a
// late edit status must not turn a dead connection back into an editable one.
class EditorConnectionTracker
{
public:
  void onConnect()
  {
    m_receivedFromServer = false;
    m_state = EditorConnectionState::connected;
  }

  void onServerMessage() { m_receivedFromServer = true; }

  void onEditStatus(const bool serverEditable)
  {
    if (!isLost())
    {
      m_state = editorConnectionForEditStatus(serverEditable);
    }
  }

  void onLoss(const bool externalServer)
  {
    if (!isLost())
    {
      m_state = editorConnectionAfterLoss(m_receivedFromServer, externalServer);
    }
  }

  [[nodiscard]] EditorConnectionState state() const noexcept { return m_state; }

  [[nodiscard]] bool isLost() const noexcept
  {
    return m_state == EditorConnectionState::connectionLost || m_state == EditorConnectionState::editTokenRejected;
  }

private:
  EditorConnectionState m_state = EditorConnectionState::connected;
  bool m_receivedFromServer = false;
};

#endif
