#include "HistoryScope.h"

namespace edits {

void HistoryScope::reset(std::optional<uuids::uuid> sceneUUID)
{
  m_sceneUUID = sceneUUID;
}

bool HistoryScope::observe(std::optional<uuids::uuid> sceneUUID)
{
  if (m_sceneUUID == sceneUUID)
  {
    return false;
  }

  m_sceneUUID = sceneUUID;
  return true;
}

}
