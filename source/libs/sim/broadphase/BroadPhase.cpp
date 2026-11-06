#include "BroadPhase.h"
#include <glm/common.hpp>
#include <algorithm>

Aabb BroadPhase::fatten(const Aabb& tight, const glm::vec3& displacement)
{
  // Stretched toward where the box is heading, so a body in steady motion leaves its fat box every few
  // ticks rather than every tick.
  const glm::vec3 reach = displacement * displacementMultiplier;

  return { tight.min - glm::vec3(margin) + glm::min(reach, glm::vec3(0.0f)),
           tight.max + glm::vec3(margin) + glm::max(reach, glm::vec3(0.0f)) };
}

uint64_t BroadPhase::makePair(const int32_t a, const int32_t b)
{
  const auto lower = static_cast<uint64_t>(static_cast<uint32_t>(std::min(a, b)));
  const auto higher = static_cast<uint64_t>(static_cast<uint32_t>(std::max(a, b)));

  return (lower << 32) | higher;
}

void BroadPhase::markMoved(const int32_t proxy)
{
  Proxy& entry = m_proxies[static_cast<size_t>(proxy)];
  if (!entry.moved)
  {
    entry.moved = true;
    m_moved.push_back(proxy);
  }
}

void BroadPhase::insertIntoTree(const int32_t proxy, const Aabb& fat)
{
  Proxy& entry = m_proxies[static_cast<size_t>(proxy)];
  entry.fat = fat;
  entry.leaf = treeOf(entry).insert(fat, proxy);
  markMoved(proxy);
}

void BroadPhase::removeFromTree(const int32_t proxy)
{
  Proxy& entry = m_proxies[static_cast<size_t>(proxy)];
  treeOf(entry).remove(entry.leaf);
  entry.leaf = DynamicAabbTree::nullNode;
  m_removedFromTree = true;
}

int32_t BroadPhase::createProxy(const Aabb& tight, const bool dynamic, const int32_t userData)
{
  int32_t proxy;
  if (m_freeProxies.empty())
  {
    proxy = static_cast<int32_t>(m_proxies.size());
    m_proxies.emplace_back();
  }
  else
  {
    proxy = m_freeProxies.back();
    m_freeProxies.pop_back();
  }

  Proxy& entry = m_proxies[static_cast<size_t>(proxy)];
  entry = Proxy{};
  entry.tight = tight;
  entry.userData = userData;
  entry.dynamic = dynamic;
  entry.alive = true;

  if (tight.isFinite())
  {
    insertIntoTree(proxy, fatten(tight, glm::vec3(0.0f)));
  }

  return proxy;
}

void BroadPhase::destroyProxy(const int32_t proxy)
{
  if (inTree(proxy))
  {
    removeFromTree(proxy);
  }

  m_proxies[static_cast<size_t>(proxy)].alive = false;
  m_pendingFree.push_back(proxy);
  m_removedFromTree = true;
}

void BroadPhase::moveProxy(const int32_t proxy, const Aabb& tight)
{
  Proxy& entry = m_proxies[static_cast<size_t>(proxy)];
  const Aabb previous = entry.tight;
  entry.tight = tight;

  if (!tight.isFinite())
  {
    if (entry.leaf != DynamicAabbTree::nullNode)
    {
      removeFromTree(proxy);
    }

    return;
  }

  if (entry.leaf == DynamicAabbTree::nullNode)
  {
    insertIntoTree(proxy, fatten(tight, glm::vec3(0.0f)));
    return;
  }

  const glm::vec3 displacement = tight.min - previous.min;
  const Aabb fat = fatten(tight, displacement);

  // A fat box that still holds the tight box stays, unless it is far larger than this tick's motion needs -
  // left stretched by a mover that has since slowed - and would keep pairing with everything it swept past.
  // The allowance is the same on every side, so a steady mover is not evicted by its trailing face.
  if (entry.fat.contains(tight))
  {
    const glm::vec3 allowance = glm::vec3(5.0f * margin) + glm::abs(displacement) * displacementMultiplier;
    const Aabb huge{ tight.min - allowance, tight.max + allowance };
    if (huge.contains(entry.fat))
    {
      return;
    }
  }

  entry.fat = fat;
  treeOf(entry).move(entry.leaf, fat);
  markMoved(proxy);
}

void BroadPhase::collectPairsOf(const int32_t proxy, QueryBlock& block) const
{
  const Proxy& entry = m_proxies[static_cast<size_t>(proxy)];

  const auto record = [this, proxy, &block](const int32_t other) {
    if (other == proxy)
    {
      return;
    }

    if (m_proxies[static_cast<size_t>(other)].moved && other < proxy)
    {
      return;
    }

    block.pairs.push_back(makePair(proxy, other));
  };

  m_dynamicTree.query(entry.fat, block.stack, record);

  if (entry.dynamic)
  {
    m_staticTree.query(entry.fat, block.stack, record);
  }
}

void BroadPhase::queryBlock(const size_t index)
{
  auto& block = m_queryBlocks[index];
  block.pairs.clear();

  const size_t begin = index * queryBlockSize;
  const size_t end = std::min(begin + queryBlockSize, m_moved.size());
  for (size_t i = begin; i < end; ++i)
  {
    if (inTree(m_moved[i]))
    {
      collectPairsOf(m_moved[i], block);
    }
  }
}

void BroadPhase::updatePairs()
{
  m_lastMovedCount = m_moved.size();

  if (m_moved.empty() && !m_removedFromTree)
  {
    return;
  }

  // Only a pair with a moved or removed side can have stopped overlapping.
  std::erase_if(m_pairs, [this](const uint64_t pair) {
    const int32_t a = firstOf(pair);
    const int32_t b = secondOf(pair);
    if (!inTree(a) || !inTree(b))
    {
      return true;
    }

    const Proxy& first = m_proxies[static_cast<size_t>(a)];
    const Proxy& second = m_proxies[static_cast<size_t>(b)];

    return (first.moved || second.moved) && !first.fat.overlaps(second.fat);
  });

  // A named, non-const local so the OpenMP clause below can list it on every compiler.
  int blockCount = static_cast<int>((m_moved.size() + queryBlockSize - 1) / queryBlockSize);
  if (m_queryBlocks.size() < static_cast<size_t>(blockCount))
  {
    m_queryBlocks.resize(static_cast<size_t>(blockCount));
  }

#pragma omp parallel for default(none) shared(blockCount) schedule(dynamic, 1) num_threads(6) if(blockCount > 4)
  for (int i = 0; i < blockCount; ++i)
  {
    queryBlock(static_cast<size_t>(i));
  }

  m_newPairs.clear();
  for (int i = 0; i < blockCount; ++i)
  {
    const auto& found = m_queryBlocks[static_cast<size_t>(i)].pairs;
    m_newPairs.insert(m_newPairs.end(), found.begin(), found.end());
  }

  if (!m_newPairs.empty())
  {
    std::ranges::sort(m_newPairs);

    const auto existing = static_cast<std::ptrdiff_t>(m_pairs.size());
    m_pairs.insert(m_pairs.end(), m_newPairs.begin(), m_newPairs.end());
    std::inplace_merge(m_pairs.begin(), m_pairs.begin() + existing, m_pairs.end());
    m_pairs.erase(std::unique(m_pairs.begin(), m_pairs.end()), m_pairs.end());
  }

  for (const int32_t proxy : m_moved)
  {
    m_proxies[static_cast<size_t>(proxy)].moved = false;
  }

  m_moved.clear();
  m_freeProxies.insert(m_freeProxies.end(), m_pendingFree.begin(), m_pendingFree.end());
  m_pendingFree.clear();
  m_removedFromTree = false;
}

void BroadPhase::clear()
{
  m_staticTree.clear();
  m_dynamicTree.clear();
  m_proxies.clear();
  m_freeProxies.clear();
  m_pendingFree.clear();
  m_moved.clear();
  m_pairs.clear();
  m_newPairs.clear();
  m_queryBlocks.clear();
  m_removedFromTree = false;
  m_lastMovedCount = 0;
}
