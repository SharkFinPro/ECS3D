#ifndef DYNAMICAABBTREE_H
#define DYNAMICAABBTREE_H

#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

struct Aabb {
  glm::vec3 min{};
  glm::vec3 max{};

  // Inclusive on every face, like the narrow phase's own box test, so boxes that only touch still pair.
  [[nodiscard]] bool overlaps(const Aabb& other) const
  {
    return min.x <= other.max.x && max.x >= other.min.x &&
           min.y <= other.max.y && max.y >= other.min.y &&
           min.z <= other.max.z && max.z >= other.min.z;
  }

  [[nodiscard]] bool contains(const Aabb& other) const
  {
    return min.x <= other.min.x && min.y <= other.min.y && min.z <= other.min.z &&
           max.x >= other.max.x && max.y >= other.max.y && max.z >= other.max.z;
  }

  [[nodiscard]] float surfaceArea() const
  {
    const glm::vec3 extent = max - min;
    return 2.0f * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x);
  }

  [[nodiscard]] bool isFinite() const;

  [[nodiscard]] static Aabb merged(const Aabb& a, const Aabb& b);
};

// An incremental bounding volume hierarchy over caller-supplied boxes: leaves are inserted by the surface
// area heuristic and the path back to the root is rebalanced with AVL rotations, so the tree stays shallow
// whatever order boxes arrive in. Node ids are stable for a leaf's lifetime and are reused after removal.
// Not thread safe to modify; concurrent queries are safe while nothing modifies it.
class DynamicAabbTree {
public:
  static constexpr int32_t nullNode = -1;

  // Every box must be finite: one NaN would poison the boxes of all its ancestors.
  [[nodiscard]] int32_t insert(const Aabb& box, int32_t userData);

  void remove(int32_t leaf);

  // Reinserts the leaf under a new box. The leaf keeps its id.
  void move(int32_t leaf, const Aabb& box);

  [[nodiscard]] const Aabb& getBox(int32_t leaf) const { return m_nodes[static_cast<size_t>(leaf)].box; }

  [[nodiscard]] int32_t getUserData(int32_t leaf) const { return m_nodes[static_cast<size_t>(leaf)].userData; }

  // Calls visit(userData) for every leaf whose box overlaps box. stack is caller-owned scratch, so a query
  // allocates nothing once it has grown.
  template <typename Visit>
  void query(const Aabb& box, std::vector<int32_t>& stack, Visit&& visit) const
  {
    if (m_root == nullNode)
    {
      return;
    }

    stack.clear();
    stack.push_back(m_root);

    while (!stack.empty())
    {
      const Node& node = m_nodes[static_cast<size_t>(stack.back())];
      stack.pop_back();

      if (!node.box.overlaps(box))
      {
        continue;
      }

      if (node.isLeaf())
      {
        visit(node.userData);
      }
      else
      {
        stack.push_back(node.left);
        stack.push_back(node.right);
      }
    }
  }

  // Zero for an empty tree or a single leaf.
  [[nodiscard]] int32_t getHeight() const;

  [[nodiscard]] size_t getLeafCount() const { return m_leafCount; }

  // Checks every link, height and enclosing box. For tests.
  [[nodiscard]] bool isValid() const;

  void clear();

private:
  struct Node {
    Aabb box;
    int32_t parent = nullNode;
    int32_t left = nullNode;
    int32_t right = nullNode;
    // Zero for a leaf, -1 for a node on the free list.
    int32_t height = 0;
    int32_t userData = -1;

    [[nodiscard]] bool isLeaf() const { return left == nullNode; }
  };

  std::vector<Node> m_nodes;
  int32_t m_root = nullNode;
  int32_t m_freeList = nullNode;
  size_t m_leafCount = 0;

  [[nodiscard]] Node& node(const int32_t index) { return m_nodes[static_cast<size_t>(index)]; }
  [[nodiscard]] const Node& node(const int32_t index) const { return m_nodes[static_cast<size_t>(index)]; }

  [[nodiscard]] int32_t allocateNode();
  void freeNode(int32_t index);

  [[nodiscard]] int32_t findBestSibling(const Aabb& box) const;
  void insertLeaf(int32_t leaf);
  void removeLeaf(int32_t leaf);

  void replaceChild(int32_t parent, int32_t oldChild, int32_t newChild);

  // Walks from index to the root, rebalancing and refitting each node on the way.
  void refitUpward(int32_t index);

  // Promotes the taller child of index when the two differ in height by more than one, and returns the
  // node now at index's place in the tree.
  [[nodiscard]] int32_t balance(int32_t index);
  [[nodiscard]] int32_t rotateUp(int32_t index, int32_t child, int32_t sibling);

  [[nodiscard]] bool isValidSubtree(int32_t index, size_t& leaves) const;
};

#endif //DYNAMICAABBTREE_H
