#ifndef PLAYERSLOTS_H
#define PLAYERSLOTS_H

#include <cstdint>
#include <optional>
#include <unordered_map>

// Binds each connection to the lowest player slot no other connection holds.
class PlayerSlots
{
public:
  // Idempotent: a connection that already holds a slot gets that slot back.
  int32_t assign(int32_t connectionId);

  // Returns the slot the connection held, or nullopt when it held none.
  std::optional<int32_t> release(int32_t connectionId);

private:
  std::unordered_map<int32_t, int32_t> m_connectionSlots;
};

#endif //PLAYERSLOTS_H
