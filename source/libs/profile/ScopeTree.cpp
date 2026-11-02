#include "ScopeTree.h"

#include <utility>

namespace profile {
  ScopeTree::ScopeTree()
  {
    m_nodes.reserve(maxNodes);
    m_nodes.emplace_back();
  }

  std::optional<NodeId> ScopeTree::child(const NodeId parent, const NameId name)
  {
    for (const auto id : m_nodes.at(parent).children)
    {
      if (m_nodes[id].name == name)
      {
        return id;
      }
    }

    if (m_nodes.size() >= maxNodes)
    {
      return std::nullopt;
    }

    const auto id = static_cast<NodeId>(m_nodes.size());
    m_nodes[parent].children.reserve(m_nodes[parent].children.size() + 1);

    Node node;
    node.parent = parent;
    node.name = name;
    node.children.reserve(4);
    m_nodes.push_back(std::move(node));
    m_nodes[parent].children.push_back(id);
    return id;
  }

  std::size_t ScopeTree::size() const
  {
    return m_nodes.size();
  }

  NodeId ScopeTree::parent(const NodeId node) const
  {
    return m_nodes.at(node).parent;
  }

  NameId ScopeTree::name(const NodeId node) const
  {
    return m_nodes.at(node).name;
  }

  const std::vector<NodeId>& ScopeTree::children(const NodeId node) const
  {
    return m_nodes.at(node).children;
  }
}
