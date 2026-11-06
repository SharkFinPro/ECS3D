#ifndef BROADPHASE_H
#define BROADPHASE_H

#include "DynamicAabbTree.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Persistent broad phase: one proxy per collider, kept in a static or a dynamic tree under a fat box (the
// tight box plus a margin, stretched along the motion of its last tick), and a sorted cache of every proxy
// pair whose fat boxes overlap. A proxy that stays inside its fat box costs one containment test a tick, and
// only proxies that left theirs are queried again, so a settled scene keeps its pairs for free. Static
// proxies never pair with each other. The cache is a superset of the tight-box overlaps: the caller filters
// it against this tick's tight boxes.
class BroadPhase {
public:
  static constexpr int32_t nullProxy = -1;

  // Matches the extents Box2D uses for bodies about a unit across, the size ECS3D scenes are built from.
  static constexpr float margin = 0.1f;
  static constexpr float displacementMultiplier = 4.0f;

  // userData is the caller's handle for the proxy, handed back with its pairs.
  [[nodiscard]] int32_t createProxy(const Aabb& tight, bool dynamic, int32_t userData);

  void destroyProxy(int32_t proxy);

  // Records this tick's tight box. A box that left its fat box, or whose fat box has grown far larger than
  // its motion needs, is reinserted and its pairs are found again by the next updatePairs. A non-finite box
  // takes the proxy out of its tree, and so out of every pair, until a finite one returns.
  void moveProxy(int32_t proxy, const Aabb& tight);

  void setUserData(int32_t proxy, int32_t userData) { m_proxies[static_cast<size_t>(proxy)].userData = userData; }

  [[nodiscard]] int32_t getUserData(int32_t proxy) const { return m_proxies[static_cast<size_t>(proxy)].userData; }

  [[nodiscard]] bool isDynamic(int32_t proxy) const { return m_proxies[static_cast<size_t>(proxy)].dynamic; }

  [[nodiscard]] const Aabb& getFatBox(int32_t proxy) const { return m_proxies[static_cast<size_t>(proxy)].fat; }

  // Brings the pair cache up to date with every create, destroy and move since the last call.
  void updatePairs();

  // Each pair as (lower proxy id << 32) | higher proxy id, ascending, no duplicates.
  [[nodiscard]] std::span<const uint64_t> getPairs() const { return m_pairs; }

  [[nodiscard]] static int32_t firstOf(const uint64_t pair) { return static_cast<int32_t>(pair >> 32); }

  [[nodiscard]] static int32_t secondOf(const uint64_t pair) { return static_cast<int32_t>(pair & 0xFFFFFFFFu); }

  [[nodiscard]] size_t getProxyCount() const { return m_proxies.size() - m_freeProxies.size() - m_pendingFree.size(); }

  // Proxies reinserted or created since the last updatePairs, then by it. For tests and the benchmark.
  [[nodiscard]] size_t getLastMovedCount() const { return m_lastMovedCount; }

  [[nodiscard]] const DynamicAabbTree& getStaticTree() const { return m_staticTree; }
  [[nodiscard]] const DynamicAabbTree& getDynamicTree() const { return m_dynamicTree; }

  void clear();

private:
  struct Proxy {
    Aabb tight;
    Aabb fat;
    int32_t leaf = DynamicAabbTree::nullNode;
    int32_t userData = -1;
    bool dynamic = false;
    bool alive = false;
    bool moved = false;
  };

  DynamicAabbTree m_staticTree;
  DynamicAabbTree m_dynamicTree;

  std::vector<Proxy> m_proxies;
  std::vector<int32_t> m_freeProxies;
  // Destroyed since the last updatePairs. Their ids are reused only after their pairs are gone.
  std::vector<int32_t> m_pendingFree;
  std::vector<int32_t> m_moved;
  bool m_removedFromTree = false;
  size_t m_lastMovedCount = 0;

  // The moved proxies are queried in fixed blocks, each into its own buffers, so the queries can run in
  // parallel without sharing anything they write.
  struct QueryBlock {
    std::vector<uint64_t> pairs;
    std::vector<int32_t> stack;
  };

  static constexpr size_t queryBlockSize = 64;

  std::vector<uint64_t> m_pairs;
  std::vector<uint64_t> m_newPairs;
  std::vector<QueryBlock> m_queryBlocks;

  [[nodiscard]] DynamicAabbTree& treeOf(const Proxy& proxy) { return proxy.dynamic ? m_dynamicTree : m_staticTree; }

  [[nodiscard]] bool inTree(const int32_t proxy) const
  {
    const Proxy& entry = m_proxies[static_cast<size_t>(proxy)];
    return entry.alive && entry.leaf != DynamicAabbTree::nullNode;
  }

  [[nodiscard]] static Aabb fatten(const Aabb& tight, const glm::vec3& displacement);

  // Takes the proxy out of its tree, and so out of every pair, if it is in one.
  void park(int32_t proxy);

  [[nodiscard]] static uint64_t makePair(int32_t a, int32_t b);

  void markMoved(int32_t proxy);
  void insertIntoTree(int32_t proxy, const Aabb& fat);
  void removeFromTree(int32_t proxy);

  // Queries one block of moved proxies into that block's own buffers. Writes nothing else, so blocks can run
  // in parallel.
  void queryBlock(size_t index);

  // Appends every pair of the moved proxy. When both proxies of a pair moved, only the lower id records it.
  // Reads the trees and proxies only.
  void collectPairsOf(int32_t proxy, QueryBlock& block) const;
};

#endif //BROADPHASE_H
