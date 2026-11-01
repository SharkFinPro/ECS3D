#ifndef FAKETRANSPORTS_H
#define FAKETRANSPORTS_H

#include <ClientTransport.h>
#include <ServerTransport.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Records every call NetServer makes and keeps the callbacks it registered, so a test can play the socket
// thread by invoking them.
class FakeServerTransport : public net::ServerTransport {
public:
  struct StartCall
  {
    int32_t port = 0;
    bool editMode = false;
    std::string authToken;
  };

  struct BroadcastCall
  {
    uint8_t type = 0;
    std::vector<uint8_t> payload;
  };

  struct SendToManyCall
  {
    std::vector<int32_t> connIds;
    uint8_t type = 0;
    std::vector<uint8_t> payload;
  };

  int32_t startResult = 1;
  int32_t connectionCountResult = 0;

  std::vector<StartCall> startCalls;
  int stopCalls = 0;
  net::ServerCallbacks callbacks;

  mutable std::vector<BroadcastCall> broadcastCalls;
  mutable std::vector<SendToManyCall> sendToManyCalls;
  mutable int connectionCountCalls = 0;

  int32_t start(const int32_t port, const bool editMode, const std::string& authToken,
                const net::ServerCallbacks& registered) override
  {
    startCalls.push_back({port, editMode, authToken});
    callbacks = registered;
    return startResult;
  }

  void stop() override
  {
    ++stopCalls;
  }

  void broadcast(const uint8_t type, const std::span<const uint8_t> payload) const override
  {
    broadcastCalls.push_back({type, {payload.begin(), payload.end()}});
  }

  void sendToMany(const std::span<const int32_t> connIds, const uint8_t type,
                  const std::span<const uint8_t> payload) const override
  {
    sendToManyCalls.push_back({{connIds.begin(), connIds.end()}, type, {payload.begin(), payload.end()}});
  }

  [[nodiscard]] int32_t connectionCount() const override
  {
    ++connectionCountCalls;
    return connectionCountResult;
  }
};

class FakeClientTransport : public net::ClientTransport {
public:
  struct ConnectCall
  {
    std::string host;
    int32_t port = 0;
    net::Role role = net::Role::player;
    std::string authToken;
  };

  struct SendCall
  {
    uint8_t type = 0;
    std::vector<uint8_t> payload;
  };

  bool connectResult = true;

  // Models the receive thread waking and reporting the drop before the managed disconnect returns.
  bool reportDisconnectInsideDisconnect = false;

  std::vector<ConnectCall> connectCalls;
  int disconnectCalls = 0;
  net::ClientCallbacks callbacks;

  mutable std::vector<SendCall> sendCalls;

  bool connect(const std::string& host, const int32_t port, const net::Role role, const std::string& authToken,
               const net::ClientCallbacks& registered) override
  {
    connectCalls.push_back({host, port, role, authToken});
    callbacks = registered;
    return connectResult;
  }

  void disconnect() override
  {
    ++disconnectCalls;

    if (reportDisconnectInsideDisconnect && callbacks.disconnect != nullptr)
    {
      callbacks.disconnect();
    }
  }

  void send(const uint8_t type, const std::span<const uint8_t> payload) const override
  {
    sendCalls.push_back({type, {payload.begin(), payload.end()}});
  }
};

#endif //FAKETRANSPORTS_H
