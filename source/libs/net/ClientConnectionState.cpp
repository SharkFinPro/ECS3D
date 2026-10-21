#include "ClientConnectionState.h"

namespace net {

void ClientConnectionState::beginConnect()
{
  m_disconnectRequested = false;
}

void ClientConnectionState::onTransportDisconnect()
{
  if (m_disconnectRequested)
  {
    return;
  }

  m_connected = false;
  m_connectionLost = true;
}

}
