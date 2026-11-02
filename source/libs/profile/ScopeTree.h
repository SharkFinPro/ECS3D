#ifndef SCOPETREE_H
#define SCOPETREE_H

#include "ProfileScope.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace profile {
  using NodeId = uint16_t;

  inline constexpr std::size_t maxNodes = 128;

  // Scope nesting interned at run time. Node 0 is the root and has no name; the same name under two parents is
  // two nodes.
  class ScopeTree {
  public:
    ScopeTree();

    // The existing child or a new one; nullopt once maxNodes is reached.
    [[nodiscard]] std::optional<NodeId> child(NodeId parent, NameId name);

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] NodeId parent(NodeId node) const;
    [[nodiscard]] NameId name(NodeId node) const;
    [[nodiscard]] const std::vector<NodeId>& children(NodeId node) const;

  private:
    struct Node {
      NodeId parent = 0;
      NameId name = 0;
      std::vector<NodeId> children;
    };

    std::vector<Node> m_nodes;
  };
}

#endif
