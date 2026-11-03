#ifndef STATSSUBSCRIPTIONS_H
#define STATSSUBSCRIPTIONS_H

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace net {

// Which connections asked for serverStats, and which node table each was last sent. Tick thread only.
class StatsSubscriptions {
public:
  // Idempotent; a new or repeated subscribe sends the node table next.
  void subscribe(int32_t connId);

  // Forgets the node-table epoch the connection was sent.
  void unsubscribe(int32_t connId);

  // For a disconnect: unsubscribes and forgets the refusal warning.
  void forget(int32_t connId);

  // True the first time per connection, so a refused subscriber is logged once.
  [[nodiscard]] bool warnRefusalOnce(int32_t connId);

  [[nodiscard]] bool empty() const;
  [[nodiscard]] bool isSubscribed(int32_t connId) const;

  struct Recipients {
    std::vector<int32_t> withNodeTable;
    std::vector<int32_t> withoutNodeTable;
  };

  // Splits the subscribers by whether they were last sent a table at a different epoch, and records epoch as sent.
  // Both lists are ascending.
  [[nodiscard]] Recipients takeRecipients(uint32_t epoch);

private:
  // 0 = no table sent yet; epochs are >= 1.
  std::unordered_map<int32_t, uint32_t> m_sentEpoch;
  std::unordered_set<int32_t> m_refusalWarned;
};

}

#endif  // STATSSUBSCRIPTIONS_H
