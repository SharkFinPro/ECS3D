#include "StatsSubscriptions.h"
#include <algorithm>

namespace net {

void StatsSubscriptions::subscribe(const int32_t connId)
{
  m_sentEpoch[connId] = 0;
}

void StatsSubscriptions::unsubscribe(const int32_t connId)
{
  m_sentEpoch.erase(connId);
}

void StatsSubscriptions::forget(const int32_t connId)
{
  unsubscribe(connId);
  m_refusalWarned.erase(connId);
}

bool StatsSubscriptions::warnRefusalOnce(const int32_t connId)
{
  return m_refusalWarned.insert(connId).second;
}

bool StatsSubscriptions::empty() const
{
  return m_sentEpoch.empty();
}

bool StatsSubscriptions::isSubscribed(const int32_t connId) const
{
  return m_sentEpoch.contains(connId);
}

StatsSubscriptions::Recipients StatsSubscriptions::takeRecipients(const uint32_t epoch)
{
  Recipients recipients;
  for (auto& [connId, sent] : m_sentEpoch)
  {
    (sent == epoch ? recipients.withoutNodeTable : recipients.withNodeTable).push_back(connId);
    sent = epoch;
  }

  std::ranges::sort(recipients.withNodeTable);
  std::ranges::sort(recipients.withoutNodeTable);
  return recipients;
}

}
