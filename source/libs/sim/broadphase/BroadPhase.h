#ifndef BROADPHASE_H
#define BROADPHASE_H

#include "DynamicAabbTree.h"
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

class Collider;

struct BroadPhaseInput {
  const Collider* key = nullptr;
  std::weak_ptr<Collider> collider;
  Aabb tight;
  glm::vec3 displacement{};
  bool dynamic = false;
  int32_t edgeIndex = -1;
};

// Two dynamic AABB trees (static and dynamic bodies) plus a persistent, sorted cache of the proxy pairs
// whose fat AABBs overlap. Only proxies that left their fat AABB this tick are re-queried, so a pair whose
// two proxies stayed put costs nothing to keep.
class BroadPhase {
public:
  struct Stats {
    size_t staticProxies = 0;
    size_t dynamicProxies = 0;
    int32_t staticHeight = 0;
    int32_t dynamicHeight = 0;
    size_t reinserts = 0;
    size_t newProxies = 0;
    size_t pairCount = 0;
  };

  void update(std::span<const BroadPhaseInput> inputs);

  // Canonical keys, (min proxy id << 32) | max proxy id, sorted ascending.
  [[nodiscard]] std::span<const uint64_t> getPairs() const { return m_pairs; }

  [[nodiscard]] static int32_t firstOf(const uint64_t pair) { return static_cast<int32_t>(pair >> 32); }

  [[nodiscard]] static int32_t secondOf(const uint64_t pair) { return static_cast<int32_t>(pair & 0xffffffffu); }

  [[nodiscard]] int32_t edgeIndexOf(int32_t proxyId) const;

  [[nodiscard]] const Stats& getStats() const { return m_stats; }

  void clear();

private:
  struct Proxy {
    const Collider* key = nullptr;
    std::weak_ptr<Collider> collider;
    bool dynamic = false;
    int32_t treeProxyId = DynamicAabbTree::nullNode;
    int32_t edgeIndex = -1;
    bool alive = false;
    bool movedThisTick = false;
    uint32_t seenStamp = 0;
  };

  DynamicAabbTree m_staticTree;
  DynamicAabbTree m_dynamicTree;

  std::vector<Proxy> m_proxies;
  std::vector<int32_t> m_freeProxies;
  std::unordered_map<const Collider*, int32_t> m_proxyByKey;

  std::vector<uint64_t> m_pairs;
  std::vector<int32_t> m_moved;
  uint32_t m_stamp = 0;
  Stats m_stats;

  [[nodiscard]] DynamicAabbTree& treeFor(const Proxy& proxy)
  {
    return proxy.dynamic ? m_dynamicTree : m_staticTree;
  }

  [[nodiscard]] const DynamicAabbTree& treeFor(const Proxy& proxy) const
  {
    return proxy.dynamic ? m_dynamicTree : m_staticTree;
  }

  [[nodiscard]] static uint64_t makePair(int32_t a, int32_t b);

  [[nodiscard]] int32_t createProxy(const BroadPhaseInput& input);
};

#endif //BROADPHASE_H
