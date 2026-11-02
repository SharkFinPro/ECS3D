#include "NetServer.h"
#include <Log.h>
#include <LogEntry.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <span>
#include <string>
#include <utility>

namespace net {

namespace {
  // One authoritative server per process; the C# socket thread routes inbound messages here. Atomic
  // because it is written on the tick thread (start/stop) and read on the socket threads with no other
  // synchronization between them - a plain pointer here would be a data race even though the callbacks
  // null-check it.
  std::atomic<NetServer*> g_activeServer{nullptr};
}

extern "C" void ecs3dNetServerReceive(const int32_t connId, const uint8_t type, const uint8_t* data, const int32_t len)
{
  if (auto* server = g_activeServer.load(std::memory_order_acquire))
  {
    server->enqueue(connId, type, data, len);
  }
}

extern "C" void ecs3dNetServerDisconnect(const int32_t connId)
{
  if (auto* server = g_activeServer.load(std::memory_order_acquire))
  {
    server->enqueueDisconnect(connId);
  }
}

extern "C" void ecs3dNetServerAuthorized(const int32_t connId, const uint8_t role)
{
  if (auto* server = g_activeServer.load(std::memory_order_acquire))
  {
    server->authorize(connId, role);
  }
}

NetServer::NetServer(const std::shared_ptr<ServerTransport>& transport)
  : m_transport(transport)
{}

// Only unregisters; the transport is not touched, so a server destroyed while started leaves its sockets
// to whoever owns the transport. The compare keeps a newer server's registration intact.
NetServer::~NetServer()
{
  auto* expected = this;
  g_activeServer.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

void NetServer::start(const int port, const bool editMode, const std::string& authToken)
{
  if (m_started)
  {
    return;
  }

  // The editMode flag is the launch-capability gate: only when set may a connection be granted
  // Role::editor at the handshake, and only if it presents authToken (when one is configured).
  m_editMode = editMode;

  g_activeServer.store(this, std::memory_order_release);

  const ServerCallbacks callbacks{&ecs3dNetServerReceive, &ecs3dNetServerDisconnect, &ecs3dNetServerAuthorized};
  int32_t result = 0;
  try
  {
    result = m_transport->start(static_cast<int32_t>(port), m_editMode, authToken, callbacks);
  }
  catch (...)
  {
    g_activeServer.store(nullptr, std::memory_order_release);
    throw;
  }

  if (result < 0)
  {
    g_activeServer.store(nullptr, std::memory_order_release);

    const auto portText = std::to_string(port);
    switch (static_cast<ServerStartError>(result))
    {
      case ServerStartError::addressInUse:
        throw std::runtime_error("Port " + portText + " is already in use by another process. "
                                 "Start the server with --port <other port>.");
      case ServerStartError::invalidPort:
        throw std::runtime_error("Port " + portText + " is out of range (0-65535).");
      default:
        throw std::runtime_error("Could not listen on port " + portText + ".");
    }
  }

  m_port = result;
  m_started = true;
}

int NetServer::port() const
{
  return m_port;
}

void NetServer::stop()
{
  if (!m_started)
  {
    return;
  }

  // The managed stop joins the accept thread and every per-connection receive thread before returning
  // (TcpBackend/WebSocketBackend ServerStop), so no socket thread can still be inside enqueue/
  // enqueueDisconnect/authorize by the time the pointer below is cleared. Clearing it after, rather than
  // before, matters only for a stray callback that could otherwise race the object's destruction; the
  // join is what actually makes that impossible.
  m_transport->stop();

  g_activeServer.store(nullptr, std::memory_order_release);
  m_started = false;
  m_port = 0;
}

void NetServer::broadcast(const Message& message) const
{
  if (!m_started)
  {
    return;
  }

  // A size above INT32_MAX would narrow to a negative or truncated frame length on the wire; refuse it
  // here rather than hand the cast something it cannot represent. This is a void hot-path callback, so
  // there is no caller to throw to - log the refusal instead.
  if (!fitsInWireFrameLength(message.size()))
  {
    Log::error(LogCategory::net, "Refusing to broadcast a " + std::to_string(message.size())
      + " byte message; the limit is " + std::to_string(std::numeric_limits<int32_t>::max()) + ".");
    m_traffic.countRefused();
    return;
  }

  // The transport's count includes a connection still in its handshake, so this can overstate the recipients.
  m_traffic.countOut(message.getType(), message.size(), static_cast<std::size_t>(std::max(connectionCount(), 0)));
  m_transport->broadcast(static_cast<uint8_t>(message.getType()), message.bytes());
}

void NetServer::sendToEditors(const Message& message) const
{
  // Copied out under the lock and the lock released before the transport is touched at all: this runs on
  // the authoritative tick thread, and the transport call is outside the editor-id bookkeeping. Holding the
  // lock across it would make authorize()/isEditor()/takeDisconnected() wait on sending, which they have
  // nothing to do with.
  sendToMany(m_connections.editorIds(), message);
}

void NetServer::sendTo(const int32_t connId, const Message& message) const
{
  sendToMany({connId}, message);
}

void NetServer::sendToMany(const std::vector<int32_t>& connIds, const Message& message) const
{
  if (!m_started)
  {
    return;
  }

  // A size above INT32_MAX would narrow to a negative or truncated frame length on the wire; refuse it
  // here rather than hand the cast something it cannot represent, the same guard broadcast() applies.
  if (!fitsInWireFrameLength(message.size()))
  {
    Log::error(LogCategory::net, "Refusing to send a " + std::to_string(message.size())
      + " byte message; the limit is " + std::to_string(std::numeric_limits<int32_t>::max()) + ".");
    m_traffic.countRefused();
    return;
  }

  if (connIds.empty())
  {
    return;
  }

  m_traffic.countOut(message.getType(), message.size(), connIds.size());

  m_transport->sendToMany(connIds, static_cast<uint8_t>(message.getType()), message.bytes());
}

int NetServer::connectionCount() const
{
  if (!m_started)
  {
    return 0;
  }

  return m_transport->connectionCount();
}

bool NetServer::poll(Message& message, int32_t& senderId)
{
  return m_inbox.pop(message, senderId);
}

void NetServer::enqueue(const int32_t connId, const uint8_t type, const uint8_t* data, const int32_t len)
{
  // An empty payload is legal (a join may carry no nonce, and ServerApp::handleJoin keys on that),
  // so a zero or negative length, or a null buffer, is an empty message rather than a range to walk.
  const auto payload = len > 0 && data != nullptr
    ? std::span<const uint8_t>(data, static_cast<std::size_t>(len))
    : std::span<const uint8_t>();

  Message message(static_cast<MessageType>(type), payload);

  m_traffic.countIn(message.getType(), message.size());
  m_inbox.push(std::move(message), connId);
}

void NetServer::enqueueDisconnect(const int32_t connId)
{
  m_connections.enqueueDisconnect(connId);
}

std::vector<int32_t> NetServer::takeDisconnected()
{
  return m_connections.takeDisconnected();
}

void NetServer::revokeEditor(const int32_t connId)
{
  m_connections.revokeEditor(connId);
}

void NetServer::authorize(const int32_t connId, const uint8_t role)
{
  m_connections.authorize(connId, role);
}

bool NetServer::isEditor(const int32_t connectionId) const
{
  return m_connections.isEditor(connectionId);
}

}
