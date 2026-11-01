#include "NetClient.h"
#include <Log.h>
#include <LogEntry.h>
#include <atomic>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace net {

namespace {
  // One connection per process; the C# socket thread routes inbound messages here. Atomic for the same
  // reason as NetServer's g_activeServer: written on the app thread, read on the socket thread, with no
  // other synchronization between the two.
  std::atomic<NetClient*> g_activeClient{nullptr};
}

extern "C" void ecs3dNetClientReceive(const uint8_t type, const uint8_t* data, const int32_t len)
{
  if (auto* client = g_activeClient.load(std::memory_order_acquire))
  {
    client->enqueue(type, data, len);
  }
}

extern "C" void ecs3dNetClientDisconnect()
{
  if (auto* client = g_activeClient.load(std::memory_order_acquire))
  {
    client->enqueueDisconnect();
  }
}

NetClient::NetClient(const std::shared_ptr<ClientTransport>& transport)
  : m_transport(transport)
{}

void NetClient::connect(const std::string& host, const int port, const Role role, const std::string& authToken)
{
  if (m_state.isConnected())
  {
    return;
  }

  m_state.beginConnect();

  g_activeClient.store(this, std::memory_order_release);

  const ClientCallbacks callbacks{&ecs3dNetClientReceive, &ecs3dNetClientDisconnect};
  const bool connected = m_transport->connect(host, static_cast<int32_t>(port), role, authToken, callbacks);

  m_state.setConnected(connected);
}

void NetClient::disconnect()
{
  if (!m_state.isConnected())
  {
    return;
  }

  // Set before the managed call: closing the socket below wakes the receive thread, which may reach
  // enqueueDisconnect before this function returns.
  m_state.requestDisconnect();

  // The managed disconnect joins the client's receive thread before returning (TcpBackend/
  // WebSocketBackend ClientDisconnect), so no socket thread can still be inside enqueue/
  // enqueueDisconnect by the time the pointer below is cleared.
  m_transport->disconnect();

  g_activeClient.store(nullptr, std::memory_order_release);
  m_state.setConnected(false);
}

void NetClient::send(const Message& message) const
{
  if (!m_state.isConnected())
  {
    return;
  }

  // A size above INT32_MAX would narrow to a negative or truncated frame length on the wire; refuse it
  // here rather than hand the cast something it cannot represent. This is a void hot-path callback, so
  // there is no caller to throw to - log the refusal instead.
  if (!fitsInWireFrameLength(message.size()))
  {
    Log::error(LogCategory::net, "Refusing to send a " + std::to_string(message.size())
      + " byte message; the limit is " + std::to_string(std::numeric_limits<int32_t>::max()) + ".");
    return;
  }

  m_transport->send(static_cast<uint8_t>(message.getType()), message.bytes());
}

bool NetClient::poll(Message& message)
{
  // The client has a single peer (the server), so the sender id is meaningless here - discard it.
  int32_t senderId = 0;
  return m_inbox.pop(message, senderId);
}

void NetClient::enqueue(const uint8_t type, const uint8_t* data, const int32_t len)
{
  // An empty payload is legal, so a zero or negative length, or a null buffer, is an empty message
  // rather than a range to walk.
  const auto payload = len > 0 && data != nullptr
    ? std::span<const uint8_t>(data, static_cast<std::size_t>(len))
    : std::span<const uint8_t>();

  Message message(static_cast<MessageType>(type), payload);

  m_inbox.push(std::move(message));
}

void NetClient::enqueueDisconnect()
{
  m_state.onTransportDisconnect();
}

bool NetClient::takeConnectionLost()
{
  return m_state.takeConnectionLost();
}

}
