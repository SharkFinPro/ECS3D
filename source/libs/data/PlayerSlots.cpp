#include "PlayerSlots.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Camera.h"
#include "objects/components/Component.h"
#include "objects/components/PlayerController.h"
#include <algorithm>

int32_t PlayerSlotTable::assign(const int32_t connId)
{
  if (const auto existing = slotOf(connId))
  {
    return *existing;
  }

  int32_t slot = 0;
  while (isHeld(slot))
  {
    ++slot;
  }

  m_connectionSlots.emplace(connId, slot);
  return slot;
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
      slots.push_back(playerController->getPlayerSlot());
    }
  }

  std::ranges::sort(slots);
  slots.erase(std::ranges::unique(slots).begin(), slots.end());
  return slots;
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
