#ifndef DYNAMICAABBTREE_H
#define DYNAMICAABBTREE_H

#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

struct Aabb {
  glm::vec3 min{};
  glm::vec3 max{};

  [[nodiscard]] bool overlaps(const Aabb& other) const
  {
    return min.x <= other.max.x && max.x >= other.min.x &&
           min.y <= other.max.y && max.y >= other.min.y &&
           min.z <= other.max.z && max.z >= other.min.z;
  }

  [[nodiscard]] bool contains(const Aabb& other) const
  {
    return min.x <= other.min.x && max.x >= other.max.x &&
           min.y <= other.min.y && max.y >= other.max.y &&
           min.z <= other.min.z && max.z >= other.max.z;
  }

  [[nodiscard]] static Aabb merged(const Aabb& a, const Aabb& b)
  {
    return { glm::vec3(a.min.x < b.min.x ? a.min.x : b.min.x,
                       a.min.y < b.min.y ? a.min.y : b.min.y,
                       a.min.z < b.min.z ? a.min.z : b.min.z),
             glm::vec3(a.max.x > b.max.x ? a.max.x : b.max.x,
                       a.max.y > b.max.y ? a.max.y : b.max.y,
                       a.max.z > b.max.z ? a.max.z : b.max.z) };
  }

  [[nodiscard]] float surfaceArea() const
  {
    const glm::vec3 d = max - min;
    return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
  }
};

// A Box2D-style incremental tree: leaves hold fat AABBs so a body that moves a little does not touch the
// tree, and insertion picks a sibling by the surface area heuristic with AVL-style rotations keeping it
// shallow.
class DynamicAabbTree {
public:
  static constexpr int32_t nullNode = -1;
  static constexpr float margin = 0.1f;
  static constexpr float displacementMultiplier = 4.0f;

  [[nodiscard]] int32_t createProxy(const Aabb& tight, int32_t userData, const glm::vec3& displacement);

  void destroyProxy(int32_t proxyId);

  // Returns true when the leaf had to be reinserted.
  bool moveProxy(int32_t proxyId, const Aabb& tight, const glm::vec3& displacement);

  [[nodiscard]] const Aabb& getFatAabb(int32_t proxyId) const;

  [[nodiscard]] int32_t getUserData(int32_t proxyId) const;

  // Calls callback(proxyId) for every leaf whose fat AABB overlaps aabb; a callback returning false stops
  // the traversal.
  template <typename Callback>
  void query(const Aabb& aabb, Callback&& callback) const
  {
    if (m_root == nullNode)
    {
      return;
    }

    std::vector<int32_t> stack;
    stack.reserve(64);
    stack.push_back(m_root);

    while (!stack.empty())
    {
      const int32_t index = stack.back();
      stack.pop_back();

      const Node& node = m_nodes[static_cast<size_t>(index)];
      if (!node.aabb.overlaps(aabb))
      {
        continue;
      }

      if (node.isLeaf())
      {
        if (!callback(index))
        {
          return;
        }
      }
      else
      {
        stack.push_back(node.child1);
        stack.push_back(node.child2);
      }
    }
  }

  [[nodiscard]] int32_t getHeight() const;

  [[nodiscard]] size_t getProxyCount() const { return m_proxyCount; }

  void clear();

private:
  struct Node {
    Aabb aabb;
    int32_t parent = nullNode;
    int32_t child1 = nullNode;
    int32_t child2 = nullNode;
    int32_t height = -1;
    int32_t userData = -1;

    [[nodiscard]] bool isLeaf() const { return child1 == nullNode; }
  };

  std::vector<Node> m_nodes;
  int32_t m_root = nullNode;
  int32_t m_freeList = nullNode;
  size_t m_proxyCount = 0;

  [[nodiscard]] static Aabb makeFat(const Aabb& tight, const glm::vec3& displacement);

  [[nodiscard]] int32_t allocateNode();
  void freeNode(int32_t index);

  void insertLeaf(int32_t leaf);
  void removeLeaf(int32_t leaf);

  // Rotates the subtree at index when its children differ in height by more than one; returns the new
  // subtree root.
  int32_t balance(int32_t index);

  void refit(int32_t index);
};

#endif //DYNAMICAABBTREE_H
