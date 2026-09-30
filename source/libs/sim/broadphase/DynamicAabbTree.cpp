#include "DynamicAabbTree.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
  [[maybe_unused]] bool isFinite(const glm::vec3& v)
  {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  }
}

Aabb DynamicAabbTree::makeFat(const Aabb& tight, const glm::vec3& displacement)
{
  Aabb fat{ tight.min - glm::vec3(margin), tight.max + glm::vec3(margin) };

  const glm::vec3 d = displacementMultiplier * displacement;

  for (int axis = 0; axis < 3; ++axis)
  {
    if (d[axis] < 0.0f)
    {
      fat.min[axis] += d[axis];
    }
    else
    {
      fat.max[axis] += d[axis];
    }
  }

  return fat;
}

int32_t DynamicAabbTree::allocateNode()
{
  if (m_freeList == nullNode)
  {
    m_nodes.emplace_back();
    m_freeList = static_cast<int32_t>(m_nodes.size()) - 1;
  }

  const int32_t index = m_freeList;
  Node& node = m_nodes[static_cast<size_t>(index)];
  m_freeList = node.parent;

  node = Node{};
  node.height = 0;

  return index;
}

void DynamicAabbTree::freeNode(const int32_t index)
{
  Node& node = m_nodes[static_cast<size_t>(index)];
  node = Node{};
  node.parent = m_freeList;
  node.height = -1;
  m_freeList = index;
}

int32_t DynamicAabbTree::createProxy(const Aabb& tight, const int32_t userData, const glm::vec3& displacement)
{
  assert(isFinite(tight.min) && isFinite(tight.max) && isFinite(displacement));

  const int32_t proxyId = allocateNode();
  Node& node = m_nodes[static_cast<size_t>(proxyId)];
  node.aabb = makeFat(tight, displacement);
  node.userData = userData;
  node.height = 0;

  insertLeaf(proxyId);
  ++m_proxyCount;

  return proxyId;
}

void DynamicAabbTree::destroyProxy(const int32_t proxyId)
{
  assert(proxyId >= 0 && static_cast<size_t>(proxyId) < m_nodes.size());
  assert(m_nodes[static_cast<size_t>(proxyId)].isLeaf());

  removeLeaf(proxyId);
  freeNode(proxyId);
  --m_proxyCount;
}

bool DynamicAabbTree::moveProxy(const int32_t proxyId, const Aabb& tight, const glm::vec3& displacement)
{
  assert(isFinite(tight.min) && isFinite(tight.max) && isFinite(displacement));

  const Node& node = m_nodes[static_cast<size_t>(proxyId)];
  assert(node.isLeaf());

  const Aabb fat = makeFat(tight, displacement);

  if (node.aabb.contains(tight))
  {
    const Aabb huge{ fat.min - glm::vec3(4.0f * margin), fat.max + glm::vec3(4.0f * margin) };
    if (huge.contains(node.aabb))
    {
      return false;
    }
  }

  removeLeaf(proxyId);
  m_nodes[static_cast<size_t>(proxyId)].aabb = fat;
  insertLeaf(proxyId);

  return true;
}

const Aabb& DynamicAabbTree::getFatAabb(const int32_t proxyId) const
{
  return m_nodes[static_cast<size_t>(proxyId)].aabb;
}

int32_t DynamicAabbTree::getUserData(const int32_t proxyId) const
{
  return m_nodes[static_cast<size_t>(proxyId)].userData;
}

int32_t DynamicAabbTree::getHeight() const
{
  return m_root == nullNode ? 0 : m_nodes[static_cast<size_t>(m_root)].height;
}

void DynamicAabbTree::clear()
{
  m_nodes.clear();
  m_root = nullNode;
  m_freeList = nullNode;
  m_proxyCount = 0;
}

void DynamicAabbTree::refit(const int32_t index)
{
  Node& node = m_nodes[static_cast<size_t>(index)];
  const Node& c1 = m_nodes[static_cast<size_t>(node.child1)];
  const Node& c2 = m_nodes[static_cast<size_t>(node.child2)];

  node.height = 1 + std::max(c1.height, c2.height);
  node.aabb = Aabb::merged(c1.aabb, c2.aabb);
}

void DynamicAabbTree::insertLeaf(const int32_t leaf)
{
  if (m_root == nullNode)
  {
    m_root = leaf;
    m_nodes[static_cast<size_t>(leaf)].parent = nullNode;
    return;
  }

  const Aabb leafAabb = m_nodes[static_cast<size_t>(leaf)].aabb;

  int32_t index = m_root;
  while (!m_nodes[static_cast<size_t>(index)].isLeaf())
  {
    const Node& node = m_nodes[static_cast<size_t>(index)];
    const int32_t child1 = node.child1;
    const int32_t child2 = node.child2;

    const float area = node.aabb.surfaceArea();
    const float combinedArea = Aabb::merged(node.aabb, leafAabb).surfaceArea();

    const float cost = 2.0f * combinedArea;
    const float inheritanceCost = 2.0f * (combinedArea - area);

    const auto descentCost = [&](const int32_t child)
    {
      const Node& c = m_nodes[static_cast<size_t>(child)];
      const float mergedArea = Aabb::merged(leafAabb, c.aabb).surfaceArea();

      return c.isLeaf() ? mergedArea + inheritanceCost : mergedArea - c.aabb.surfaceArea() + inheritanceCost;
    };

    const float cost1 = descentCost(child1);
    const float cost2 = descentCost(child2);

    if (cost < cost1 && cost < cost2)
    {
      break;
    }

    index = cost1 < cost2 ? child1 : child2;
  }

  const int32_t sibling = index;
  const int32_t oldParent = m_nodes[static_cast<size_t>(sibling)].parent;

  // allocateNode may reallocate m_nodes, so no node reference is held across it.
  const int32_t newParent = allocateNode();
  Node& parentNode = m_nodes[static_cast<size_t>(newParent)];
  parentNode.parent = oldParent;
  parentNode.userData = -1;
  parentNode.aabb = Aabb::merged(leafAabb, m_nodes[static_cast<size_t>(sibling)].aabb);
  parentNode.height = m_nodes[static_cast<size_t>(sibling)].height + 1;
  parentNode.child1 = sibling;
  parentNode.child2 = leaf;

  m_nodes[static_cast<size_t>(sibling)].parent = newParent;
  m_nodes[static_cast<size_t>(leaf)].parent = newParent;

  if (oldParent != nullNode)
  {
    Node& grand = m_nodes[static_cast<size_t>(oldParent)];
    if (grand.child1 == sibling)
    {
      grand.child1 = newParent;
    }
    else
    {
      grand.child2 = newParent;
    }
  }
  else
  {
    m_root = newParent;
  }

  index = m_nodes[static_cast<size_t>(leaf)].parent;
  while (index != nullNode)
  {
    index = balance(index);
    refit(index);
    index = m_nodes[static_cast<size_t>(index)].parent;
  }
}

void DynamicAabbTree::removeLeaf(const int32_t leaf)
{
  if (leaf == m_root)
  {
    m_root = nullNode;
    return;
  }

  const int32_t parent = m_nodes[static_cast<size_t>(leaf)].parent;
  const int32_t grand = m_nodes[static_cast<size_t>(parent)].parent;
  const Node& parentNode = m_nodes[static_cast<size_t>(parent)];
  const int32_t sibling = parentNode.child1 == leaf ? parentNode.child2 : parentNode.child1;

  if (grand != nullNode)
  {
    Node& grandNode = m_nodes[static_cast<size_t>(grand)];
    if (grandNode.child1 == parent)
    {
      grandNode.child1 = sibling;
    }
    else
    {
      grandNode.child2 = sibling;
    }

    m_nodes[static_cast<size_t>(sibling)].parent = grand;
    freeNode(parent);

    int32_t index = grand;
    while (index != nullNode)
    {
      index = balance(index);
      refit(index);
      index = m_nodes[static_cast<size_t>(index)].parent;
    }
  }
  else
  {
    m_root = sibling;
    m_nodes[static_cast<size_t>(sibling)].parent = nullNode;
    freeNode(parent);
  }
}

int32_t DynamicAabbTree::balance(const int32_t iA)
{
  Node& a = m_nodes[static_cast<size_t>(iA)];
  if (a.isLeaf() || a.height < 2)
  {
    return iA;
  }

  const int32_t iB = a.child1;
  const int32_t iC = a.child2;
  Node& b = m_nodes[static_cast<size_t>(iB)];
  Node& c = m_nodes[static_cast<size_t>(iC)];

  const auto replaceInParent = [this](const int32_t oldChild, const int32_t newChild, const int32_t parent)
  {
    if (parent == nullNode)
    {
      m_root = newChild;
      return;
    }

    Node& p = m_nodes[static_cast<size_t>(parent)];
    if (p.child1 == oldChild)
    {
      p.child1 = newChild;
    }
    else
    {
      p.child2 = newChild;
    }
  };

  const int32_t imbalance = c.height - b.height;

  if (imbalance > 1)
  {
    const int32_t iF = c.child1;
    const int32_t iG = c.child2;
    Node& f = m_nodes[static_cast<size_t>(iF)];
    Node& g = m_nodes[static_cast<size_t>(iG)];

    c.child1 = iA;
    c.parent = a.parent;
    a.parent = iC;
    replaceInParent(iA, iC, c.parent);

    if (f.height > g.height)
    {
      c.child2 = iF;
      a.child2 = iG;
      g.parent = iA;
      a.aabb = Aabb::merged(b.aabb, g.aabb);
      c.aabb = Aabb::merged(a.aabb, f.aabb);
      a.height = 1 + std::max(b.height, g.height);
      c.height = 1 + std::max(a.height, f.height);
    }
    else
    {
      c.child2 = iG;
      a.child2 = iF;
      f.parent = iA;
      a.aabb = Aabb::merged(b.aabb, f.aabb);
      c.aabb = Aabb::merged(a.aabb, g.aabb);
      a.height = 1 + std::max(b.height, f.height);
      c.height = 1 + std::max(a.height, g.height);
    }

    return iC;
  }

  if (imbalance < -1)
  {
    const int32_t iD = b.child1;
    const int32_t iE = b.child2;
    Node& d = m_nodes[static_cast<size_t>(iD)];
    Node& e = m_nodes[static_cast<size_t>(iE)];

    b.child1 = iA;
    b.parent = a.parent;
    a.parent = iB;
    replaceInParent(iA, iB, b.parent);

    if (d.height > e.height)
    {
      b.child2 = iD;
      a.child1 = iE;
      e.parent = iA;
      a.aabb = Aabb::merged(c.aabb, e.aabb);
      b.aabb = Aabb::merged(a.aabb, d.aabb);
      a.height = 1 + std::max(c.height, e.height);
      b.height = 1 + std::max(a.height, d.height);
    }
    else
    {
      b.child2 = iE;
      a.child1 = iD;
      d.parent = iA;
      a.aabb = Aabb::merged(c.aabb, d.aabb);
      b.aabb = Aabb::merged(a.aabb, e.aabb);
      a.height = 1 + std::max(c.height, d.height);
      b.height = 1 + std::max(a.height, e.height);
    }

    return iB;
  }

  return iA;
}
