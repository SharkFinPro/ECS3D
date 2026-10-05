#include "PlayerSlots.h"

SlotAssignment PlayerSlots::assign(const int32_t connectionId)
{
  if (const auto it = m_connectionSlots.find(connectionId); it != m_connectionSlots.end())
  {
    return {it->second, false};
  }

  int32_t slot = 0;
  const auto slotTaken = [this](const int32_t candidate) {
    for (const auto& [conn, taken] : m_connectionSlots)
    {
      if (taken == candidate)
      {
        return true;
      }
    }
    return false;
  };
  while (slotTaken(slot))
  {
    ++slot;
  }

  m_connectionSlots.emplace(connectionId, slot);
  return {slot, true};
}

std::optional<int32_t> PlayerSlots::release(const int32_t connectionId)
{
  const auto it = m_connectionSlots.find(connectionId);
  if (it == m_connectionSlots.end())
  {
    return std::nullopt;
  }

  const int32_t slot = it->second;
  m_connectionSlots.erase(it);
  return slot;
}
