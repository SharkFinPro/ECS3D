#ifndef CLIENTCONNECTIONSTATE_H
#define CLIENTCONNECTIONSTATE_H

#include <atomic>

namespace net {

// NetClient's connection flags and the rules between them. The app thread connects, disconnects and polls;
// the transport's socket thread reports a dropped connection, so every flag is atomic.
class ClientConnectionState {
public:
  [[nodiscard]] bool isConnected() const { return m_connected; }

  // A fresh connection attempt: any earlier lifecycle's requested disconnect no longer applies.
  void beginConnect();

  void setConnected(bool connected) { m_connected = connected; }

  // Marks the next transport drop as asked for. Call before the managed disconnect closes the socket,
  // which may wake the receive thread before the disconnect returns.
  void requestDisconnect() { m_disconnectRequested = true; }

  // Called on the socket thread when the receive loop exits. A requested disconnect is not a lost
  // connection and is ignored; any other drop clears connected and raises the lost flag.
  void onTransportDisconnect();

  // Returns whether the connection was lost since the last call, clearing the flag.
  [[nodiscard]] bool takeConnectionLost() { return m_connectionLost.exchange(false); }

private:
  std::atomic<bool> m_connected = false;
  std::atomic<bool> m_connectionLost = false;
  std::atomic<bool> m_disconnectRequested = false;
};

}

#endif //CLIENTCONNECTIONSTATE_H
