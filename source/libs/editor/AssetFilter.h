#ifndef ASSETFILTER_H
#define ASSETFILTER_H

#include <assets/AssetRegistry.h>
#include <bit>
#include <cstdint>

// The Assets panel's type filter: a set of asset types shown as a union. An empty set shows everything.
// AssetType::Unknown is never a member; it is not a type a user can filter by.
class AssetTypeFilter {
public:
  void toggle(const AssetType type)
  {
    set(type, !isActive(type));
  }

  void set(const AssetType type, const bool active)
  {
    if (!isConcrete(type))
    {
      return;
    }

    if (active)
    {
      m_mask |= bit(type);
    }
    else
    {
      m_mask &= ~bit(type);
    }
  }

  void clear()
  {
    m_mask = 0;
  }

  [[nodiscard]] bool isActive(const AssetType type) const
  {
    return isConcrete(type) && (m_mask & bit(type)) != 0;
  }

  [[nodiscard]] bool empty() const
  {
    return m_mask == 0;
  }

  [[nodiscard]] bool matches(const AssetType type) const
  {
    return empty() || isActive(type);
  }

  [[nodiscard]] int count() const
  {
    return std::popcount(m_mask);
  }

  [[nodiscard]] bool operator==(const AssetTypeFilter&) const = default;

private:
  [[nodiscard]] static bool isConcrete(const AssetType type)
  {
    return type != AssetType::Unknown;
  }

  [[nodiscard]] static std::uint32_t bit(const AssetType type)
  {
    return std::uint32_t{1} << static_cast<std::uint32_t>(type);
  }

  std::uint32_t m_mask = 0;
};

#endif //ASSETFILTER_H
