#include "PlayerSlots.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Camera.h"
#include "objects/components/Component.h"
#include "objects/components/PlayerController.h"
#include <algorithm>

std::optional<int32_t> PlayerSlotTable::assign(const int32_t connId)
{
  if (const auto existing = slotOf(connId))
  {
    return existing;
  }

  for (int32_t slot = 0; slot <= maxPlayerSlot; ++slot)
  {
    if (!isHeld(slot))
    {
      m_connectionSlots.emplace(connId, slot);
      return slot;
    }
  }

  return std::nullopt;
}

std::optional<int32_t> PlayerSlotTable::release(const int32_t connId)
{
  const auto it = m_connectionSlots.find(connId);
  if (it == m_connectionSlots.end())
  {
    return std::nullopt;
  }

  const int32_t slot = it->second;
  m_connectionSlots.erase(it);
  return slot;
}

std::optional<int32_t> PlayerSlotTable::slotOf(const int32_t connId) const
{
  const auto it = m_connectionSlots.find(connId);
  if (it == m_connectionSlots.end())
  {
    return std::nullopt;
  }

  return it->second;
}

std::vector<int32_t> PlayerSlotTable::heldSlots() const
{
  std::vector<int32_t> slots;
  slots.reserve(m_connectionSlots.size());
  for (const auto& entry : m_connectionSlots)
  {
    slots.push_back(entry.second);
  }

  std::ranges::sort(slots);
  return slots;
}

PossessOutcome PlayerSlotTable::request(const int32_t connId, const int32_t slot)
{
  if (slot < 0 || slot > maxPlayerSlot)
  {
    return { PossessResult::outOfRange, std::nullopt };
  }

  const auto current = slotOf(connId);
  if (current == slot)
  {
    return { PossessResult::unchanged, std::nullopt };
  }

  if (isHeld(slot))
  {
    return { PossessResult::held, std::nullopt };
  }

  m_connectionSlots[connId] = slot;
  return { PossessResult::granted, current };
}

bool PlayerSlotTable::isHeld(const int32_t slot) const
{
  return std::ranges::any_of(m_connectionSlots, [slot](const auto& entry) { return entry.second == slot; });
}

std::vector<int32_t> playerSlotsInScene(const ObjectManager& objectManager)
{
  std::vector<int32_t> slots;

  for (const auto& object : objectManager.getAllObjects())
  {
    if (const auto playerController = object->getComponent<PlayerController>(ComponentType::playerController))
    {
      const int32_t slot = playerController->getPlayerSlot();
      if (slot >= 0 && slot <= maxPlayerSlot)
      {
        slots.push_back(slot);
      }
    }
  }

  std::ranges::sort(slots);
  slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
  return slots;
}

std::string permittedViewCamera(const ObjectManager& objectManager, const int32_t slot,
                                const std::string& requested, const bool connectionIsEditor)
{
  const auto parsed = uuids::uuid::from_string(requested);
  if (!parsed)
  {
    return {};
  }

  if (!connectionIsEditor && findPlayerCamera(objectManager, slot) != parsed)
  {
    return {};
  }

  return requested;
}

std::optional<uuids::uuid> findPlayerCamera(const ObjectManager& objectManager, const int32_t slot)
{
  for (const auto& object : objectManager.getAllObjects())
  {
    const auto playerController = object->getComponent<PlayerController>(ComponentType::playerController);
    if (!playerController || playerController->getPlayerSlot() != slot)
    {
      continue;
    }

    if (object->getComponent<Camera>(ComponentType::camera))
    {
      return object->getUUID();
    }
  }

  return std::nullopt;
}
