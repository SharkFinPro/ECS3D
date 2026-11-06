// Research harness: replays recorded per-tick collider bounds through several broad-phase algorithms,
// checks that each one finds exactly the reference pair set, and times them. Also times main's own
// gather / bounds / sort / sweep stages in isolation against the live scene.
#include "BenchScenes.h"

#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/Collider.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace candidates {
  struct Box {
    glm::vec3 min;
    glm::vec3 max;

    [[nodiscard]] bool overlaps(const Box& o) const
    {
      return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y &&
             min.z <= o.max.z && max.z >= o.min.z;
    }

    [[nodiscard]] bool contains(const Box& o) const
    {
      return min.x <= o.min.x && min.y <= o.min.y && min.z <= o.min.z && max.x >= o.max.x &&
             max.y >= o.max.y && max.z >= o.max.z;
    }

    [[nodiscard]] float area() const
    {
      const glm::vec3 d = max - min;
      return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
    }

    [[nodiscard]] static Box merge(const Box& a, const Box& b)
    {
      return { glm::min(a.min, b.min), glm::max(a.max, b.max) };
    }
  };

  using Pairs = std::vector<uint64_t>;

  inline uint64_t key(const uint32_t a, const uint32_t b)
  {
    return a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a;
  }

  struct Frame {
    std::vector<Box> boxes;
  };

  struct World {
    std::vector<char> dynamic;
    std::vector<Frame> frames;
  };

  struct Algorithm {
    virtual ~Algorithm() = default;
    virtual const char* name() const = 0;
    // Called once with the first frame.
    virtual void init(const World&, const Frame&) {}
    // Appends every pair with at least one dynamic side whose tight boxes overlap. Order is free.
    virtual void step(const World& world, const Frame& frame, Pairs& out) = 0;
    size_t tested = 0;
  };

  // --- reference: brute force over dynamic x all ---------------------------------------------------
  struct Brute final : Algorithm {
    const char* name() const override { return "brute"; }
    void step(const World& w, const Frame& f, Pairs& out) override
    {
      const auto n = static_cast<uint32_t>(f.boxes.size());
      for (uint32_t i = 0; i < n; ++i)
      {
        for (uint32_t j = i + 1; j < n; ++j)
        {
          if ((w.dynamic[i] || w.dynamic[j]) && f.boxes[i].overlaps(f.boxes[j]))
          {
            out.push_back(key(i, j));
          }
        }
      }
    }
  };

  // --- main's sweep: full sort by min x each tick, forward scan ------------------------------------
  struct SapFullSort final : Algorithm {
    int axis = 0;
    bool bestAxis = false;
    std::vector<std::pair<float, uint32_t>> order;
    const char* name() const override { return bestAxis ? "sap-best-axis" : "sap-x-sort"; }
    void step(const World& w, const Frame& f, Pairs& out) override
    {
      const auto n = static_cast<uint32_t>(f.boxes.size());
      if (bestAxis)
      {
        glm::vec3 s(0), s2(0);
        for (const auto& b : f.boxes)
        {
          const glm::vec3 c = (b.min + b.max) * 0.5f;
          s += c;
          s2 += c * c;
        }
        const glm::vec3 var = s2 - s * s / float(n);
        axis = var.x >= var.y && var.x >= var.z ? 0 : (var.y >= var.z ? 1 : 2);
      }
      order.resize(n);
      for (uint32_t i = 0; i < n; ++i)
      {
        order[i] = { f.boxes[i].min[axis], i };
      }
      std::sort(order.begin(), order.end());
      for (uint32_t a = 0; a < n; ++a)
      {
        const uint32_t i = order[a].second;
        const Box& bi = f.boxes[i];
        for (uint32_t b = a + 1; b < n && order[b].first <= bi.max[axis]; ++b)
        {
          const uint32_t j = order[b].second;
          if (!w.dynamic[i] && !w.dynamic[j])
          {
            continue;
          }
          ++tested;
          if (bi.overlaps(f.boxes[j]))
          {
            out.push_back(key(i, j));
          }
        }
      }
    }
  };

  // --- persistent sweep: insertion sort on the previous tick's order ------------------------------
  struct SapInsertion final : Algorithm {
    std::vector<uint32_t> order;
    std::vector<float> keys;
    const char* name() const override { return "sap-x-insertion"; }
    void init(const World&, const Frame& f) override
    {
      order.resize(f.boxes.size());
      std::iota(order.begin(), order.end(), 0u);
    }
    void step(const World& w, const Frame& f, Pairs& out) override
    {
      const size_t n = order.size();
      keys.resize(n);
      for (size_t a = 0; a < n; ++a)
      {
        keys[a] = f.boxes[order[a]].min.x;
      }
      for (size_t a = 1; a < n; ++a)
      {
        const float k = keys[a];
        const uint32_t id = order[a];
        size_t b = a;
        while (b > 0 && keys[b - 1] > k)
        {
          keys[b] = keys[b - 1];
          order[b] = order[b - 1];
          --b;
        }
        keys[b] = k;
        order[b] = id;
      }
      for (size_t a = 0; a < n; ++a)
      {
        const uint32_t i = order[a];
        const Box& bi = f.boxes[i];
        for (size_t b = a + 1; b < n && keys[b] <= bi.max.x; ++b)
        {
          const uint32_t j = order[b];
          if (!w.dynamic[i] && !w.dynamic[j])
          {
            continue;
          }
          ++tested;
          if (bi.overlaps(f.boxes[j]))
          {
            out.push_back(key(i, j));
          }
        }
      }
    }
  };

  // --- uniform grid: static cells built once, dynamic cells rebuilt each tick ----------------------
  struct Grid final : Algorithm {
    float cell = 2.0f;
    int largeCells = 64;
    struct Entry {
      uint64_t cell;
      uint32_t id;
      bool operator<(const Entry& o) const { return cell < o.cell || (cell == o.cell && id < o.id); }
    };
    std::vector<Entry> staticEntries;
    std::vector<uint32_t> staticLarge;
    std::vector<Entry> dynamicEntries;
    std::vector<uint32_t> dynamicLarge;
    std::vector<uint32_t> dynamicIds;
    const char* name() const override { return "grid"; }

    static uint64_t cellKey(int x, int y, int z)
    {
      return (uint64_t(uint32_t(x + (1 << 20)) & 0x1FFFFF) << 42) | (uint64_t(uint32_t(y + (1 << 20)) & 0x1FFFFF) << 21) |
             (uint64_t(uint32_t(z + (1 << 20)) & 0x1FFFFF));
    }
    void range(const Box& b, glm::ivec3& lo, glm::ivec3& hi) const
    {
      lo = glm::ivec3(glm::floor(b.min / cell));
      hi = glm::ivec3(glm::floor(b.max / cell));
    }
    template <typename F>
    void forCells(const glm::ivec3& lo, const glm::ivec3& hi, F&& f) const
    {
      for (int x = lo.x; x <= hi.x; ++x)
        for (int y = lo.y; y <= hi.y; ++y)
          for (int z = lo.z; z <= hi.z; ++z)
            f(cellKey(x, y, z));
    }
    void insert(const Box& b, uint32_t id, std::vector<Entry>& entries, std::vector<uint32_t>& large) const
    {
      glm::ivec3 lo, hi;
      range(b, lo, hi);
      const glm::ivec3 d = hi - lo + 1;
      if (int64_t(d.x) * d.y * d.z > largeCells)
      {
        large.push_back(id);
        return;
      }
      forCells(lo, hi, [&](uint64_t c) { entries.push_back({ c, id }); });
    }
    // A pair sharing several cells is reported only from the cell holding the corner of their overlap.
    bool ownsPair(uint64_t c, const Box& a, const Box& b) const
    {
      const glm::vec3 corner = glm::max(a.min, b.min);
      const glm::ivec3 k(glm::floor(corner / cell));
      return cellKey(k.x, k.y, k.z) == c;
    }
    void init(const World& w, const Frame& f) override
    {
      // Cell size: twice the median dynamic extent.
      std::vector<float> sizes;
      for (size_t i = 0; i < f.boxes.size(); ++i)
      {
        if (w.dynamic[i])
        {
          const glm::vec3 d = f.boxes[i].max - f.boxes[i].min;
          sizes.push_back(std::max({ d.x, d.y, d.z }));
        }
      }
      if (!sizes.empty())
      {
        std::nth_element(sizes.begin(), sizes.begin() + sizes.size() / 2, sizes.end());
        cell = std::max(0.25f, 2.0f * sizes[sizes.size() / 2]);
      }
      for (uint32_t i = 0; i < f.boxes.size(); ++i)
      {
        if (!w.dynamic[i])
        {
          insert(f.boxes[i], i, staticEntries, staticLarge);
        }
        else
        {
          dynamicIds.push_back(i);
        }
      }
      std::sort(staticEntries.begin(), staticEntries.end());
    }
    void step(const World&, const Frame& f, Pairs& out) override
    {
      dynamicEntries.clear();
      dynamicLarge.clear();
      for (const uint32_t i : dynamicIds)
      {
        insert(f.boxes[i], i, dynamicEntries, dynamicLarge);
      }
      std::sort(dynamicEntries.begin(), dynamicEntries.end());
      // dynamic vs dynamic, per cell
      for (size_t s = 0; s < dynamicEntries.size();)
      {
        size_t e = s + 1;
        while (e < dynamicEntries.size() && dynamicEntries[e].cell == dynamicEntries[s].cell)
        {
          ++e;
        }
        for (size_t a = s; a < e; ++a)
        {
          for (size_t b = a + 1; b < e; ++b)
          {
            const uint32_t i = dynamicEntries[a].id, j = dynamicEntries[b].id;
            ++tested;
            if (f.boxes[i].overlaps(f.boxes[j]) && ownsPair(dynamicEntries[a].cell, f.boxes[i], f.boxes[j]))
            {
              out.push_back(key(i, j));
            }
          }
        }
        s = e;
      }
      // dynamic vs static: probe the static cells
      for (const auto& d : dynamicEntries)
      {
        const auto it = std::lower_bound(staticEntries.begin(), staticEntries.end(), Entry{ d.cell, 0 });
        for (auto s = it; s != staticEntries.end() && s->cell == d.cell; ++s)
        {
          ++tested;
          if (f.boxes[d.id].overlaps(f.boxes[s->id]) && ownsPair(d.cell, f.boxes[d.id], f.boxes[s->id]))
          {
            out.push_back(key(d.id, s->id));
          }
        }
      }
      // large ones against everything they could touch
      for (const uint32_t l : dynamicLarge)
      {
        for (uint32_t j = 0; j < f.boxes.size(); ++j)
        {
          if (j != l && f.boxes[l].overlaps(f.boxes[j]))
          {
            // dynamic large vs dynamic large would be found twice
            const bool otherLargeDyn = std::find(dynamicLarge.begin(), dynamicLarge.end(), j) != dynamicLarge.end();
            if (!otherLargeDyn || l < j)
            {
              out.push_back(key(l, j));
            }
          }
          ++tested;
        }
      }
      for (const uint32_t l : staticLarge)
      {
        for (const uint32_t j : dynamicIds)
        {
          ++tested;
          const bool jLarge = std::find(dynamicLarge.begin(), dynamicLarge.end(), j) != dynamicLarge.end();
          if (!jLarge && f.boxes[l].overlaps(f.boxes[j]))
          {
            out.push_back(key(l, j));
          }
        }
      }
    }
  };

  // --- dynamic AABB tree ---------------------------------------------------------------------------
  class Tree {
  public:
    static constexpr int32_t null = -1;
    struct Node {
      Box box;
      int32_t parent = null;
      int32_t left = null;
      int32_t right = null;
      int32_t height = 0;
      uint32_t id = 0;
      bool leaf() const { return left == null; }
    };
    std::vector<Node> nodes;
    int32_t root = null;
    int32_t freeList = null;

    int32_t alloc()
    {
      if (freeList != null)
      {
        const int32_t n = freeList;
        freeList = nodes[n].parent;
        nodes[n] = Node{};
        return n;
      }
      nodes.emplace_back();
      return int32_t(nodes.size() - 1);
    }
    void release(int32_t n)
    {
      nodes[n].parent = freeList;
      nodes[n].height = -1;
      freeList = n;
    }
    int32_t insert(const Box& box, uint32_t id)
    {
      const int32_t leaf = alloc();
      nodes[leaf].box = box;
      nodes[leaf].id = id;
      insertLeaf(leaf);
      return leaf;
    }
    void remove(int32_t leaf)
    {
      removeLeaf(leaf);
      release(leaf);
    }
    void insertLeaf(int32_t leaf)
    {
      if (root == null)
      {
        root = leaf;
        nodes[leaf].parent = null;
        return;
      }
      const Box lb = nodes[leaf].box;
      int32_t index = root;
      while (!nodes[index].leaf())
      {
        const int32_t l = nodes[index].left, r = nodes[index].right;
        const float area = nodes[index].box.area();
        const float combined = Box::merge(nodes[index].box, lb).area();
        const float cost = 2.0f * combined;
        const float inherit = 2.0f * (combined - area);
        auto childCost = [&](int32_t c) {
          const float merged = Box::merge(lb, nodes[c].box).area();
          return nodes[c].leaf() ? merged + inherit : merged - nodes[c].box.area() + inherit;
        };
        const float cl = childCost(l), cr = childCost(r);
        if (cost < cl && cost < cr)
        {
          break;
        }
        index = cl < cr ? l : r;
      }
      const int32_t sibling = index;
      const int32_t oldParent = nodes[sibling].parent;
      const int32_t parent = alloc();
      nodes[parent].parent = oldParent;
      nodes[parent].box = Box::merge(lb, nodes[sibling].box);
      nodes[parent].height = nodes[sibling].height + 1;
      nodes[parent].left = sibling;
      nodes[parent].right = leaf;
      nodes[sibling].parent = parent;
      nodes[leaf].parent = parent;
      if (oldParent != null)
      {
        (nodes[oldParent].left == sibling ? nodes[oldParent].left : nodes[oldParent].right) = parent;
      }
      else
      {
        root = parent;
      }
      refitFrom(parent);
    }
    void removeLeaf(int32_t leaf)
    {
      if (leaf == root)
      {
        root = null;
        return;
      }
      const int32_t parent = nodes[leaf].parent;
      const int32_t grand = nodes[parent].parent;
      const int32_t sibling = nodes[parent].left == leaf ? nodes[parent].right : nodes[parent].left;
      if (grand != null)
      {
        (nodes[grand].left == parent ? nodes[grand].left : nodes[grand].right) = sibling;
        nodes[sibling].parent = grand;
        release(parent);
        refitFrom(grand);
      }
      else
      {
        root = sibling;
        nodes[sibling].parent = null;
        release(parent);
      }
    }
    void refitFrom(int32_t index)
    {
      while (index != null)
      {
        index = balance(index);
        const int32_t l = nodes[index].left, r = nodes[index].right;
        nodes[index].height = 1 + std::max(nodes[l].height, nodes[r].height);
        nodes[index].box = Box::merge(nodes[l].box, nodes[r].box);
        index = nodes[index].parent;
      }
    }
    int32_t balance(int32_t a)
    {
      Node& A = nodes[a];
      if (A.leaf() || A.height < 2)
      {
        return a;
      }
      const int32_t b = A.left, c = A.right;
      const int32_t diff = nodes[c].height - nodes[b].height;
      if (diff > 1)
      {
        return rotate(a, c, b);
      }
      if (diff < -1)
      {
        return rotate(a, b, c);
      }
      return a;
    }
    // Promotes the taller child `up` above `a`; `other` stays under a.
    int32_t rotate(int32_t a, int32_t up, int32_t other)
    {
      const int32_t f = nodes[up].left, g = nodes[up].right;
      nodes[up].left = a;
      nodes[up].parent = nodes[a].parent;
      nodes[a].parent = up;
      if (nodes[up].parent != null)
      {
        int32_t p = nodes[up].parent;
        (nodes[p].left == a ? nodes[p].left : nodes[p].right) = up;
      }
      else
      {
        root = up;
      }
      const bool keepF = nodes[f].height > nodes[g].height;
      const int32_t keep = keepF ? f : g, give = keepF ? g : f;
      nodes[up].right = keep;
      if (nodes[a].left == up)
      {
        nodes[a].left = give;
      }
      else
      {
        nodes[a].right = give;
      }
      nodes[give].parent = a;
      nodes[a].box = Box::merge(nodes[other].box, nodes[give].box);
      nodes[a].height = 1 + std::max(nodes[other].height, nodes[give].height);
      nodes[up].box = Box::merge(nodes[a].box, nodes[keep].box);
      nodes[up].height = 1 + std::max(nodes[a].height, nodes[keep].height);
      return up;
    }
    template <typename F>
    void query(const Box& box, F&& f, std::vector<int32_t>& stack) const
    {
      if (root == null)
      {
        return;
      }
      stack.clear();
      stack.push_back(root);
      while (!stack.empty())
      {
        const int32_t i = stack.back();
        stack.pop_back();
        const Node& n = nodes[i];
        if (!n.box.overlaps(box))
        {
          continue;
        }
        if (n.leaf())
        {
          f(n.id);
        }
        else
        {
          stack.push_back(n.left);
          stack.push_back(n.right);
        }
      }
    }
  };

  inline Box fatten(const Box& b, float margin)
  {
    return { b.min - glm::vec3(margin), b.max + glm::vec3(margin) };
  }

  // Static tree once; dynamic tree with fat leaves; each dynamic queries both trees every tick.
  struct TreeQueryAll final : Algorithm {
    float margin = 0.1f;
    Tree staticTree, dynamicTree;
    std::vector<int32_t> leafOf;
    std::vector<Box> fat;
    std::vector<uint32_t> dynamicIds;
    std::vector<int32_t> stack;
    size_t reinserts = 0;
    const char* name() const override { return "tree-query-all"; }
    void init(const World& w, const Frame& f) override
    {
      leafOf.assign(f.boxes.size(), Tree::null);
      fat.resize(f.boxes.size());
      for (uint32_t i = 0; i < f.boxes.size(); ++i)
      {
        if (w.dynamic[i])
        {
          dynamicIds.push_back(i);
          fat[i] = fatten(f.boxes[i], margin);
          leafOf[i] = dynamicTree.insert(fat[i], i);
        }
        else
        {
          leafOf[i] = staticTree.insert(f.boxes[i], i);
        }
      }
    }
    void step(const World& w, const Frame& f, Pairs& out) override
    {
      for (const uint32_t i : dynamicIds)
      {
        if (!fat[i].contains(f.boxes[i]))
        {
          dynamicTree.remove(leafOf[i]);
          fat[i] = fatten(f.boxes[i], margin);
          leafOf[i] = dynamicTree.insert(fat[i], i);
          ++reinserts;
        }
      }
      for (const uint32_t i : dynamicIds)
      {
        const Box& b = f.boxes[i];
        staticTree.query(b, [&](uint32_t j) {
          ++tested;
          out.push_back(key(i, j));
        }, stack);
        dynamicTree.query(b, [&](uint32_t j) {
          if (j > i)
          {
            ++tested;
            if (b.overlaps(f.boxes[j]))
            {
              out.push_back(key(i, j));
            }
          }
        }, stack);
      }
      (void)w;
    }
  };

  // Prototype design: fat-box pair cache, only proxies that left their fat box re-query.
  struct TreePairCache final : Algorithm {
    float margin = 0.1f;
    float multiplier = 0.0f;
    std::vector<Box> previous;
    std::string label = "tree-pair-cache";
    Tree staticTree, dynamicTree;
    std::vector<int32_t> leafOf;
    std::vector<Box> fat;
    std::vector<uint32_t> dynamicIds;
    std::vector<uint32_t> moved;
    std::vector<char> isMoved;
    std::vector<int32_t> stack;
    Pairs cache;
    size_t reinserts = 0;
    const char* name() const override { return label.c_str(); }
    Box predict(uint32_t i, const Box& b) const
    {
      Box fatBox = fatten(b, margin);
      const glm::vec3 d = (b.min - previous[i].min) * multiplier;
      fatBox.min += glm::min(d, glm::vec3(0));
      fatBox.max += glm::max(d, glm::vec3(0));
      return fatBox;
    }
    void init(const World& w, const Frame& f) override
    {
      previous = f.boxes;
      leafOf.assign(f.boxes.size(), Tree::null);
      fat.resize(f.boxes.size());
      isMoved.assign(f.boxes.size(), 0);
      for (uint32_t i = 0; i < f.boxes.size(); ++i)
      {
        if (w.dynamic[i])
        {
          dynamicIds.push_back(i);
          fat[i] = fatten(f.boxes[i], margin);
          leafOf[i] = dynamicTree.insert(fat[i], i);
          moved.push_back(i);
          isMoved[i] = 1;
        }
        else
        {
          fat[i] = f.boxes[i];
          leafOf[i] = staticTree.insert(f.boxes[i], i);
        }
      }
    }
    void step(const World& w, const Frame& f, Pairs& out) override
    {
      for (const uint32_t i : dynamicIds)
      {
        if (!fat[i].contains(f.boxes[i]))
        {
          dynamicTree.remove(leafOf[i]);
          fat[i] = predict(i, f.boxes[i]);
          leafOf[i] = dynamicTree.insert(fat[i], i);
          ++reinserts;
          if (!isMoved[i])
          {
            isMoved[i] = 1;
            moved.push_back(i);
          }
        }
      }
      if (!moved.empty())
      {
        // Drop cached pairs whose fat boxes separated, then add the moved proxies' new ones.
        std::erase_if(cache, [&](uint64_t p) {
          const uint32_t a = uint32_t(p >> 32), b = uint32_t(p);
          return (isMoved[a] || isMoved[b]) && !fat[a].overlaps(fat[b]);
        });
        const size_t before = cache.size();
        for (const uint32_t i : moved)
        {
          staticTree.query(fat[i], [&](uint32_t j) { cache.push_back(key(i, j)); }, stack);
          dynamicTree.query(fat[i], [&](uint32_t j) {
            if (j != i && (!isMoved[j] || j > i))
            {
              cache.push_back(key(i, j));
            }
          }, stack);
        }
        std::sort(cache.begin() + before, cache.end());
        std::inplace_merge(cache.begin(), cache.begin() + before, cache.end());
        cache.erase(std::unique(cache.begin(), cache.end()), cache.end());
        for (const uint32_t i : moved)
        {
          isMoved[i] = 0;
        }
        moved.clear();
      }
      for (const uint32_t i : dynamicIds)
      {
        previous[i] = f.boxes[i];
      }
      for (const uint64_t p : cache)
      {
        ++tested;
        const uint32_t a = uint32_t(p >> 32), b = uint32_t(p);
        if (f.boxes[a].overlaps(f.boxes[b]))
        {
          out.push_back(p);
        }
      }
      (void)w;
    }
  };

  // Static tree for dynamic-vs-static, a persistent sweep over the dynamic bodies only for dynamic pairs.
  struct StaticTreeDynamicSap final : Algorithm {
    Tree staticTree;
    std::vector<uint32_t> order;
    std::vector<float> keys;
    std::vector<int32_t> stack;
    const char* name() const override { return "static-tree+dynamic-sap"; }
    void init(const World& w, const Frame& f) override
    {
      for (uint32_t i = 0; i < f.boxes.size(); ++i)
      {
        if (w.dynamic[i])
        {
          order.push_back(i);
        }
        else
        {
          staticTree.insert(f.boxes[i], i);
        }
      }
    }
    void step(const World&, const Frame& f, Pairs& out) override
    {
      const size_t n = order.size();
      keys.resize(n);
      for (size_t a = 0; a < n; ++a)
      {
        keys[a] = f.boxes[order[a]].min.x;
      }
      for (size_t a = 1; a < n; ++a)
      {
        const float k = keys[a];
        const uint32_t id = order[a];
        size_t b = a;
        while (b > 0 && keys[b - 1] > k)
        {
          keys[b] = keys[b - 1];
          order[b] = order[b - 1];
          --b;
        }
        keys[b] = k;
        order[b] = id;
      }
      for (size_t a = 0; a < n; ++a)
      {
        const uint32_t i = order[a];
        const Box& bi = f.boxes[i];
        for (size_t b = a + 1; b < n && keys[b] <= bi.max.x; ++b)
        {
          ++tested;
          if (bi.overlaps(f.boxes[order[b]]))
          {
            out.push_back(key(i, order[b]));
          }
        }
        staticTree.query(bi, [&](uint32_t j) {
          ++tested;
          out.push_back(key(i, j));
        }, stack);
      }
    }
  };

  World record(fixtures::Scene& scene, int ticks)
  {
    World world;
    std::vector<std::shared_ptr<Collider>> colliders;
    for (const auto& object : scene.objectManager->getAllObjects())
    {
      if (auto c = object->getComponent<Collider>(ComponentType::collider))
      {
        colliders.push_back(c);
        world.dynamic.push_back(object->getComponent<RigidBody>(ComponentType::rigidBody) != nullptr);
      }
    }
    CollisionSystem collisions;
    for (int t = 0; t < ticks; ++t)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, 1.0f / 50.0f);
      collisions.fixedUpdate(*scene.objectManager, 1.0f / 50.0f);
      Frame frame;
      frame.boxes.reserve(colliders.size());
      for (const auto& c : colliders)
      {
        const auto& b = c->getBoundingBox();
        frame.boxes.push_back({ { b.minX, b.minY, b.minZ }, { b.maxX, b.maxY, b.maxZ } });
      }
      world.frames.push_back(std::move(frame));
    }
    return world;
  }

  // Main's own per-tick stages, replayed against the live (settled) scene with nothing moving, so only
  // the bookkeeping is timed.
  void timeMainStages(const fixtures::Scene& scene, int reps)
  {
    struct Edge {
      std::shared_ptr<Object> object;
      std::shared_ptr<Collider> collider;
      float position;
      std::shared_ptr<RigidBody> body;
      BoundingBox bounds{};
      uint32_t layer = 0;
      uint32_t mask = 0;
    };
    std::vector<Edge> edges;
    using Clock = std::chrono::steady_clock;
    double gather = 0, bounds = 0, sort = 0, scan = 0;
    size_t visits = 0;
    for (int r = 0; r < reps; ++r)
    {
      auto t0 = Clock::now();
      edges.clear();
      for (const auto& object : scene.objectManager->getAllObjects())
      {
        if (const auto collider = object->getComponent<Collider>(ComponentType::collider))
        {
          if (!object->getComponents().contains(ComponentType::transform))
          {
            continue;
          }
          edges.push_back({ object, collider, 0.0f, object->getComponent<RigidBody>(ComponentType::rigidBody) });
        }
      }
      auto t1 = Clock::now();
      for (auto& e : edges)
      {
        e.bounds = e.collider->getBoundingBox();
        e.position = e.bounds.minX;
        e.layer = e.collider->getLayer();
        e.mask = e.collider->getMask();
      }
      auto t2 = Clock::now();
      std::ranges::sort(edges, [](const Edge& a, const Edge& b) { return a.position < b.position; });
      auto t3 = Clock::now();
      size_t hits = 0;
      for (size_t i = 0; i < edges.size(); ++i)
      {
        const auto& bb = edges[i].bounds;
        for (size_t j = i + 1; j < edges.size(); ++j)
        {
          if (edges[j].position > bb.maxX)
          {
            break;
          }
          ++visits;
          if (!edges[i].body && !edges[j].body)
          {
            continue;
          }
          const auto& o = edges[j].bounds;
          if (bb.maxX < o.minX || bb.minX > o.maxX || bb.maxY < o.minY || bb.minY > o.maxY || bb.maxZ < o.minZ ||
              bb.minZ > o.maxZ)
          {
            continue;
          }
          ++hits;
        }
      }
      auto t4 = Clock::now();
      gather += std::chrono::duration<double, std::micro>(t1 - t0).count();
      bounds += std::chrono::duration<double, std::micro>(t2 - t1).count();
      sort += std::chrono::duration<double, std::micro>(t3 - t2).count();
      scan += std::chrono::duration<double, std::micro>(t4 - t3).count();
      if (hits == size_t(-1))
      {
        std::cout << "";
      }
    }
    std::cout << std::fixed << std::setprecision(1) << "  main stages (settled scene, serial) us/tick: gather "
              << gather / reps << ", bounds " << bounds / reps << ", sort " << sort / reps << ", x-scan "
              << scan / reps << " (" << visits / reps << " visits)\n";
  }

  int run(const bench::SceneOptions& options, int ticks, bool withBrute)
  {
    auto scene = bench::buildScene(options);
    const World world = record(scene, ticks);
    std::cout << "scene=" << options.name << " size=" << options.size << " layers=" << options.layers
              << " colliders=" << world.dynamic.size()
              << " dynamic=" << std::count(world.dynamic.begin(), world.dynamic.end(), 1) << " ticks=" << ticks << "\n";
    timeMainStages(scene, 20);

    std::vector<std::unique_ptr<Algorithm>> algorithms;
    if (withBrute)
    {
      algorithms.push_back(std::make_unique<Brute>());
    }
    algorithms.push_back(std::make_unique<SapFullSort>());
    {
      auto best = std::make_unique<SapFullSort>();
      best->bestAxis = true;
      algorithms.push_back(std::move(best));
    }
    algorithms.push_back(std::make_unique<SapInsertion>());
    algorithms.push_back(std::make_unique<Grid>());
    algorithms.push_back(std::make_unique<TreeQueryAll>());
    for (const float margin : { 0.1f })
    {
      for (const float multiplier : { 0.0f, 4.0f })
      {
        auto tree = std::make_unique<TreePairCache>();
        tree->margin = margin;
        tree->multiplier = multiplier;
        tree->label = "tree-cache m" + std::to_string(margin).substr(0, 4) + " k" + std::to_string(int(multiplier));
        algorithms.push_back(std::move(tree));
      }
    }
    algorithms.push_back(std::make_unique<StaticTreeDynamicSap>());

    std::vector<Pairs> reference;
    int failures = 0;
    for (auto& algorithm : algorithms)
    {
      using Clock = std::chrono::steady_clock;
      const auto i0 = Clock::now();
      algorithm->init(world, world.frames.front());
      const double initMicros = std::chrono::duration<double, std::micro>(Clock::now() - i0).count();
      double micros = 0;
      size_t pairs = 0;
      bool exact = true;
      Pairs out;
      for (size_t t = 0; t < world.frames.size(); ++t)
      {
        out.clear();
        const auto s = Clock::now();
        algorithm->step(world, world.frames[t], out);
        micros += std::chrono::duration<double, std::micro>(Clock::now() - s).count();
        std::sort(out.begin(), out.end());
        const bool duplicates = std::adjacent_find(out.begin(), out.end()) != out.end();
        pairs += out.size();
        if (reference.size() <= t)
        {
          reference.push_back(out);
        }
        else if (out != reference[t] || duplicates)
        {
          exact = false;
        }
      }
      failures += !exact;
      std::cout << std::fixed << std::setprecision(1) << "  " << std::left << std::setw(26) << algorithm->name()
                << std::right << " us/tick " << std::setw(9) << micros / ticks << "  init us " << std::setw(9)
                << initMicros << "  pairs/tick " << std::setw(8) << double(pairs) / ticks << "  tested/tick "
                << std::setw(10) << double(algorithm->tested) / ticks << (exact ? "  exact" : "  MISMATCH") << "\n";
    }
    return failures;
  }
}

int main(const int argc, char** argv)
{
  bench::SceneOptions options;
  int ticks = 150;
  bool brute = false;
  for (int i = 1; i + 1 < argc; i += 2)
  {
    const std::string flag = argv[i], value = argv[i + 1];
    if (flag == "--scene") options.name = value;
    else if (flag == "--size") options.size = std::stoi(value);
    else if (flag == "--layers") options.layers = std::stoi(value);
    else if (flag == "--seed") options.seed = uint32_t(std::stoul(value));
    else if (flag == "--ticks") ticks = std::stoi(value);
    else if (flag == "--brute") brute = value == "1";
  }
  return candidates::run(options, ticks, brute);
}
