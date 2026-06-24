#include "NetServer.h"

namespace net {

NetServer::NetServer(std::shared_ptr<ManagedHost> host)
  : m_host(std::move(host))
{}

void NetServer::start(const int port, const bool editMode)
{
  // TODO: launch the C# websocket server (loaded through ManagedHost) listening on port. The
  // TODO:   editMode flag is the launch-capability gate: only when set may a connection be
  // TODO:   granted Role::Editor (via the auth token at handshake).
  m_editMode = editMode;
  (void)port;
}

void NetServer::stop()
{
  // TODO: signal the transport thread to close all connections and shut down.
}

void NetServer::broadcast(const Message& message)
{
  // TODO: hand the message to the C# transport for every connected client (snapshots on join,
  // TODO:   state deltas per tick). Queued so the tick thread never blocks on the socket thread.
  m_outbox.push(message);
}

bool NetServer::poll(Message& message)
{
  // TODO: drain one inbound message (client input / edit commands) collected by the socket thread.
  return m_inbox.pop(message);
}

}
