#ifndef PLAYERSLOTS_H
#define PLAYERSLOTS_H

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <uuid.h>
#include <vector>

class ObjectManager;

// The highest player slot a connection may be bound to; matches the range the PlayerController
// inspector offers.
inline constexpr int32_t maxPlayerSlot = 255;

enum class PossessResult {
  granted,
  unchanged,
  held,
  outOfRange
};

struct PossessOutcome {
  PossessResult result = PossessResult::outOfRange;
  // The slot the connection held before the request; set only when the request was granted and the
  // connection had one.
  std::optional<int32_t> previousSlot;
};

// Which connection holds which player slot. A slot has at most one holder, and a request never takes a
// slot another live connection holds.
class PlayerSlotTable {
public:
  // Binds connId to the lowest free slot, or returns the slot it already holds.
  int32_t assign(int32_t connId);

  // Returns the slot that was freed, if the connection held one.
  std::optional<int32_t> release(int32_t connId);

  [[nodiscard]] std::optional<int32_t> slotOf(int32_t connId) const;

  PossessOutcome request(int32_t connId, int32_t slot);

private:
  std::unordered_map<int32_t, int32_t> m_connectionSlots;

  [[nodiscard]] bool isHeld(int32_t slot) const;
};

// Every PlayerController slot in the scene that a connection could be bound to (0..maxPlayerSlot), children
// included, ascending and without duplicates.
[[nodiscard]] std::vector<int32_t> playerSlotsInScene(const ObjectManager& objectManager);

// The first object whose PlayerController holds the slot and that also carries a Camera.
[[nodiscard]] std::optional<uuids::uuid> findPlayerCamera(const ObjectManager& objectManager, int32_t slot);

#endif //PLAYERSLOTS_H
