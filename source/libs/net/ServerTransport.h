#ifndef SERVERTRANSPORT_H
#define SERVERTRANSPORT_H

#include <cstdint>
#include <span>
#include <string>

namespace net {

// Matches ECS3DNetTransport.ServerStartError (Transport.cs): what a transport's start returns on failure.
enum class ServerStartError : int32_t
{
  addressInUse = -1,
  invalidPort = -2,
  failed = -3,
};

struct ServerCallbacks
{
  void (*receive)(int32_t connId, uint8_t type, const uint8_t* data, int32_t len) = nullptr;
  void (*disconnect)(int32_t connId) = nullptr;
  void (*authorized)(int32_t connId, uint8_t role) = nullptr;
};

class ServerTransport {
public:
  virtual ~ServerTransport() = default;

  // Registers the callbacks, then starts listening. Returns the bound port, or a negative ServerStartError.
  virtual int32_t start(int32_t port, bool editMode, const std::string& authToken,
                        const ServerCallbacks& callbacks) = 0;

  virtual void stop() = 0;

  virtual void broadcast(uint8_t type, std::span<const uint8_t> payload) const = 0;

  virtual void sendToMany(std::span<const int32_t> connIds, uint8_t type,
                          std::span<const uint8_t> payload) const = 0;

  [[nodiscard]] virtual int32_t connectionCount() const = 0;
};

}

#endif //SERVERTRANSPORT_H
