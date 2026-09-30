#include "BroadPhase.h"
#include <algorithm>
#include <cassert>

uint64_t BroadPhase::makePair(const int32_t a, const int32_t b)
{
  const auto lo = static_cast<uint64_t>(std::min(a, b));
  const auto hi = static_cast<uint64_t>(std::max(a, b));

  return (lo << 32) | hi;
}

int32_t BroadPhase::edgeIndexOf(const int32_t proxyId) const
{
  return m_proxies[static_cast<size_t>(proxyId)].edgeIndex;
}

void BroadPhase::clear()
{
  m_staticTree.clear();
  m_dynamicTree.clear();
  m_proxies.clear();
  m_freeProxies.clear();
  m_proxyByKey.clear();
  m_pairs.clear();
  m_moved.clear();
  m_stats = {};
}

int32_t BroadPhase::createProxy(const BroadPhaseInput& input)
{
  int32_t id;
  if (m_freeProxies.empty())
  {
    id = static_cast<int32_t>(m_proxies.size());
    m_proxies.emplace_back();
  }
  else
  {
    id = m_freeProxies.back();
    m_freeProxies.pop_back();
  }

  Proxy& proxy = m_proxies[static_cast<size_t>(id)];
  proxy.key = input.key;
  proxy.collider = input.collider;
  proxy.dynamic = input.dynamic;
  proxy.edgeIndex = input.edgeIndex;
  proxy.alive = true;
  proxy.movedThisTick = true;
  proxy.seenStamp = m_stamp;
  proxy.treeProxyId = treeFor(proxy).createProxy(input.tight, id, input.displacement);

  m_proxyByKey[input.key] = id;

  return id;
}

void BroadPhase::collectPairsFor(const int32_t id, std::vector<uint64_t>& hits) const
{
  const Proxy& proxy = m_proxies[static_cast<size_t>(id)];
  const Aabb fat = treeFor(proxy).getFatAabb(proxy.treeProxyId);

  const auto addHit = [&hits, id](const DynamicAabbTree& tree, const int32_t treeId)
  {
    const int32_t other = tree.getUserData(treeId);
    if (other != id)
    {
      hits.push_back(makePair(id, other));
    }

    return true;
  };

  m_dynamicTree.query(fat, [&](const int32_t treeId) { return addHit(m_dynamicTree, treeId); });

  if (proxy.dynamic)
  {
    m_staticTree.query(fat, [&](const int32_t treeId) { return addHit(m_staticTree, treeId); });
  }
}

void BroadPhase::update(const std::span<const BroadPhaseInput> inputs)
{
  m_stats.reinserts = 0;
  m_stats.newProxies = 0;

  ++m_stamp;
  m_moved.clear();

  for (auto& proxy : m_proxies)
  {
    proxy.movedThisTick = false;
  }

  std::vector<size_t> toCreate;

  for (size_t i = 0; i < inputs.size(); ++i)
  {
    const auto& input = inputs[i];

    const auto found = m_proxyByKey.find(input.key);
    if (found == m_proxyByKey.end())
    {
      toCreate.push_back(i);
      continue;
    }

    Proxy& proxy = m_proxies[static_cast<size_t>(found->second)];

    // An expired or different collider at the same address is a new collider, and a body that gained or
    // lost a RigidBody changes trees; neither can keep its proxy. It is left unseen so the sweep below
    // destroys it.
    if (proxy.collider.expired() || proxy.collider.lock().get() != input.key || proxy.dynamic != input.dynamic)
    {
      toCreate.push_back(i);
      continue;
    }

    proxy.edgeIndex = input.edgeIndex;
    proxy.seenStamp = m_stamp;

    if (treeFor(proxy).moveProxy(proxy.treeProxyId, input.tight, input.displacement))
    {
      proxy.movedThisTick = true;
      m_moved.push_back(found->second);
      ++m_stats.reinserts;
    }
  }

  std::vector<int32_t> dead;
  for (size_t id = 0; id < m_proxies.size(); ++id)
  {
    Proxy& proxy = m_proxies[id];
    if (!proxy.alive || proxy.seenStamp == m_stamp)
    {
      continue;
    }

    treeFor(proxy).destroyProxy(proxy.treeProxyId);
    m_proxyByKey.erase(proxy.key);
    proxy.alive = false;
    proxy.collider.reset();
    dead.push_back(static_cast<int32_t>(id));
  }

  // Pairs must go before any dead id is handed out again, or a new proxy would inherit a stranger's pairs.
  if (!dead.empty())
  {
    std::erase_if(m_pairs, [this](const uint64_t pair)
    {
      return !m_proxies[static_cast<size_t>(firstOf(pair))].alive ||
             !m_proxies[static_cast<size_t>(secondOf(pair))].alive;
    });

    m_freeProxies.insert(m_freeProxies.end(), dead.begin(), dead.end());
  }

  for (const auto i : toCreate)
  {
    m_moved.push_back(createProxy(inputs[i]));
    ++m_stats.newProxies;
  }

  const size_t existingPairs = m_pairs.size();

  // The trees and proxies are only read from here on, so each moved proxy queries into its own list. The pair
  // vector is sorted and deduplicated below, which makes the merge order irrelevant.
  const int movedCount = static_cast<int>(m_moved.size());
  const int threads = m_threadCount;
  std::vector<std::vector<uint64_t>> found(m_moved.size());

#pragma omp parallel for num_threads(threads) schedule(dynamic, 16)
  for (int i = 0; i < movedCount; ++i)
  {
    collectPairsFor(m_moved[static_cast<size_t>(i)], found[static_cast<size_t>(i)]);
  }

  for (const auto& hits : found)
  {
    m_pairs.insert(m_pairs.end(), hits.begin(), hits.end());
  }

  if (m_pairs.size() != existingPairs)
  {
    std::sort(m_pairs.begin(), m_pairs.end());
    m_pairs.erase(std::unique(m_pairs.begin(), m_pairs.end()), m_pairs.end());
  }

  std::erase_if(m_pairs, [this](const uint64_t pair)
  {
    const Proxy& a = m_proxies[static_cast<size_t>(firstOf(pair))];
    const Proxy& b = m_proxies[static_cast<size_t>(secondOf(pair))];

    return !treeFor(a).getFatAabb(a.treeProxyId).overlaps(treeFor(b).getFatAabb(b.treeProxyId));
  });

  m_stats.staticProxies = m_staticTree.getProxyCount();
  m_stats.dynamicProxies = m_dynamicTree.getProxyCount();
  m_stats.staticHeight = m_staticTree.getHeight();
  m_stats.dynamicHeight = m_dynamicTree.getHeight();
  m_stats.pairCount = m_pairs.size();
}
