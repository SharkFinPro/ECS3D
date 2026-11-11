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

// Only a token the user gave for a server the editor did not spawn can be wrong.
[[nodiscard]] constexpr bool editorHasExternalServerToken(const bool launchedLocalServer,
                                                          const bool tokenSupplied) noexcept
{
  return !launchedLocalServer && tokenSupplied;
}

// A server that accepted the handshake sends a snapshot and an edit status straight away, so an editor that was
// given a token for an external server and was dropped before receiving anything was refused at the handshake.
// The transport reports no distinct close reason, so a server that refused the editor for another reason lands
// here too. The token of a server the editor spawned is its own, so a drop from that one is never a rejection.
[[nodiscard]] constexpr EditorConnectionState editorConnectionAfterLoss(const bool receivedFromServer,
                                                                        const bool externalServerToken) noexcept
{
  if (!receivedFromServer && externalServerToken)
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
      return "The server closed the connection before sending anything: the edit token was rejected, or the server "
             "refused the editor. Check the --token value.";
    default:
      return "Connection to the server was lost. Save your work and restart the editor.";
  }
}

#endif
