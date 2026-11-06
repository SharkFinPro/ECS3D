#include "DynamicAabbTree.h"
#include <glm/common.hpp>
#include <algorithm>
#include <cmath>

bool Aabb::isFinite() const
{
  for (int axis = 0; axis < 3; ++axis)
  {
    if (!std::isfinite(min[axis]) || !std::isfinite(max[axis]))
    {
      return false;
    }
  }

  return true;
}

Aabb Aabb::merged(const Aabb& a, const Aabb& b)
{
  return { glm::min(a.min, b.min), glm::max(a.max, b.max) };
}

int32_t DynamicAabbTree::allocateNode()
{
  if (m_freeList == nullNode)
  {
    m_nodes.emplace_back();
    return static_cast<int32_t>(m_nodes.size() - 1);
  }

  const int32_t index = m_freeList;
  m_freeList = node(index).parent;
  node(index) = Node{};

  return index;
}

void DynamicAabbTree::freeNode(const int32_t index)
{
  Node& freed = node(index);
  freed.parent = m_freeList;
  freed.left = nullNode;
  freed.right = nullNode;
  freed.height = -1;
  m_freeList = index;
}

int32_t DynamicAabbTree::insert(const Aabb& box, const int32_t userData)
{
  const int32_t leaf = allocateNode();
  node(leaf).box = box;
  node(leaf).userData = userData;

  insertLeaf(leaf);
  ++m_leafCount;

  return leaf;
}

void DynamicAabbTree::remove(const int32_t leaf)
{
  removeLeaf(leaf);
  freeNode(leaf);
  --m_leafCount;
}

void DynamicAabbTree::move(const int32_t leaf, const Aabb& box)
{
  removeLeaf(leaf);
  node(leaf).box = box;
  insertLeaf(leaf);
}

int32_t DynamicAabbTree::findBestSibling(const Aabb& box) const
{
  // Descends toward the child whose cost of taking the new leaf, plus the area every ancestor grows by, is
  // lowest, and stops where pairing with the current node is cheaper than going further down.
  int32_t index = m_root;
  while (!node(index).isLeaf())
  {
    const Node& current = node(index);
    const float area = current.box.surfaceArea();
    const float combinedArea = Aabb::merged(current.box, box).surfaceArea();

    const float pairCost = 2.0f * combinedArea;
    const float inheritedCost = 2.0f * (combinedArea - area);

    const auto descendCost = [&](const int32_t child) {
      const Node& candidate = node(child);
      const float mergedArea = Aabb::merged(box, candidate.box).surfaceArea();
      return candidate.isLeaf() ? mergedArea + inheritedCost
                                : mergedArea - candidate.box.surfaceArea() + inheritedCost;
    };

    const float leftCost = descendCost(current.left);
    const float rightCost = descendCost(current.right);

    if (pairCost < leftCost && pairCost < rightCost)
    {
      break;
    }

    index = leftCost < rightCost ? current.left : current.right;
  }

  return index;
}

void DynamicAabbTree::insertLeaf(const int32_t leaf)
{
  if (m_root == nullNode)
  {
    m_root = leaf;
    node(leaf).parent = nullNode;
    return;
  }

  const int32_t sibling = findBestSibling(node(leaf).box);
  const int32_t oldParent = node(sibling).parent;

  const int32_t newParent = allocateNode();
  Node& parent = node(newParent);
  parent.parent = oldParent;
  parent.left = sibling;
  parent.right = leaf;
  parent.box = Aabb::merged(node(leaf).box, node(sibling).box);
  parent.height = node(sibling).height + 1;

  node(sibling).parent = newParent;
  node(leaf).parent = newParent;

  if (oldParent == nullNode)
  {
    m_root = newParent;
  }
  else
  {
    replaceChild(oldParent, sibling, newParent);
  }

  refitUpward(node(leaf).parent);
}

void DynamicAabbTree::removeLeaf(const int32_t leaf)
{
  if (leaf == m_root)
  {
    m_root = nullNode;
    return;
  }

  const int32_t parent = node(leaf).parent;
  const int32_t grandParent = node(parent).parent;
  const int32_t sibling = node(parent).left == leaf ? node(parent).right : node(parent).left;

  freeNode(parent);

  if (grandParent == nullNode)
  {
    m_root = sibling;
    node(sibling).parent = nullNode;
    return;
  }

  replaceChild(grandParent, parent, sibling);
  node(sibling).parent = grandParent;
  refitUpward(grandParent);
}

void DynamicAabbTree::replaceChild(const int32_t parent, const int32_t oldChild, const int32_t newChild)
{
  Node& target = node(parent);
  if (target.left == oldChild)
  {
    target.left = newChild;
  }
  else
  {
    target.right = newChild;
  }
}

void DynamicAabbTree::refitUpward(int32_t index)
{
  while (index != nullNode)
  {
    index = balance(index);

    Node& current = node(index);
    const Node& left = node(current.left);
    const Node& right = node(current.right);
    current.height = 1 + std::max(left.height, right.height);
    current.box = Aabb::merged(left.box, right.box);

    index = current.parent;
  }
}

int32_t DynamicAabbTree::balance(const int32_t index)
{
  const Node& current = node(index);
  if (current.isLeaf() || current.height < 2)
  {
    return index;
  }

  const int32_t left = current.left;
  const int32_t right = current.right;
  const int32_t imbalance = node(right).height - node(left).height;

  if (imbalance > 1)
  {
    return rotateUp(index, right, left);
  }

  if (imbalance < -1)
  {
    return rotateUp(index, left, right);
  }

  return index;
}

int32_t DynamicAabbTree::rotateUp(const int32_t index, const int32_t child, const int32_t sibling)
{
  // child takes index's place; index keeps sibling plus the shorter of child's two children, and child
  // keeps the taller one beside index.
  const int32_t grandLeft = node(child).left;
  const int32_t grandRight = node(child).right;
  const int32_t parent = node(index).parent;

  node(child).parent = parent;
  node(index).parent = child;
  if (parent == nullNode)
  {
    m_root = child;
  }
  else
  {
    replaceChild(parent, index, child);
  }

  const bool keepLeft = node(grandLeft).height > node(grandRight).height;
  const int32_t kept = keepLeft ? grandLeft : grandRight;
  const int32_t given = keepLeft ? grandRight : grandLeft;

  node(child).left = index;
  node(child).right = kept;

  replaceChild(index, child, given);
  node(given).parent = index;

  Node& lowered = node(index);
  lowered.box = Aabb::merged(node(sibling).box, node(given).box);
  lowered.height = 1 + std::max(node(sibling).height, node(given).height);

  Node& raised = node(child);
  raised.box = Aabb::merged(lowered.box, node(kept).box);
  raised.height = 1 + std::max(lowered.height, node(kept).height);

  return child;
}

int32_t DynamicAabbTree::getHeight() const
{
  return m_root == nullNode ? 0 : node(m_root).height;
}

void DynamicAabbTree::clear()
{
  m_nodes.clear();
  m_root = nullNode;
  m_freeList = nullNode;
  m_leafCount = 0;
}

bool DynamicAabbTree::isValid() const
{
  size_t freeNodes = 0;
  for (int32_t index = m_freeList; index != nullNode; index = node(index).parent)
  {
    if (++freeNodes > m_nodes.size() || node(index).height != -1)
    {
      return false;
    }
  }

  if (m_root == nullNode)
  {
    return m_leafCount == 0 && freeNodes == m_nodes.size();
  }

  if (node(m_root).parent != nullNode)
  {
    return false;
  }

  size_t leaves = 0;
  if (!isValidSubtree(m_root, leaves) || leaves != m_leafCount)
  {
    return false;
  }

  return 2 * leaves - 1 + freeNodes == m_nodes.size();
}

bool DynamicAabbTree::isValidSubtree(const int32_t index, size_t& leaves) const
{
  const Node& current = node(index);
  if (current.isLeaf())
  {
    ++leaves;
    return current.right == nullNode && current.height == 0;
  }

  if (current.right == nullNode || node(current.left).parent != index || node(current.right).parent != index)
  {
    return false;
  }

  const Node& left = node(current.left);
  const Node& right = node(current.right);
  if (current.height != 1 + std::max(left.height, right.height))
  {
    return false;
  }

  const Aabb expected = Aabb::merged(left.box, right.box);
  if (expected.min != current.box.min || expected.max != current.box.max)
  {
    return false;
  }

  return isValidSubtree(current.left, leaves) && isValidSubtree(current.right, leaves);
}
