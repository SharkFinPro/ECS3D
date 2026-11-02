#include "ScopeTree.h"

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
    m_nodes.emplace_back();
    m_nodes.back().parent = parent;
    m_nodes.back().name = name;
    m_nodes.back().children.reserve(4);
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
