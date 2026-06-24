#include "NetClient.h"

namespace net {

NetClient::NetClient(std::shared_ptr<ManagedHost> host)
  : m_host(std::move(host))
{}

void NetClient::connect(const std::string& host, const int port, const Role role, const std::string& authToken)
{
  // TODO: open the C# websocket connection (loaded through ManagedHost) to host:port. role +
  // TODO:   authToken are sent at the handshake; the server grants Role::Editor only if its
  // TODO:   edit-mode launch gate is enabled and the token authorizes it. Same wire format for
  // TODO:   singleplayer (loopback), local-MP, and remote.
  (void)host;
  (void)port;
  (void)role;
  (void)authToken;
}

void NetClient::disconnect()
{
  // TODO: close the connection and stop the transport thread.
}

void NetClient::send(const Message& message)
{
  // TODO: queue an outbound message (input state, or edit commands from the editor).
  m_outbox.push(message);
}

bool NetClient::poll(Message& message)
{
  // TODO: drain one inbound message (snapshot on join, state delta per tick) for the view to apply.
  return m_inbox.pop(message);
}

}
