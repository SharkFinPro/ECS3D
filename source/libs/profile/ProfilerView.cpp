#include "ProfilerView.h"

#include "ProfileStats.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace profile {
  namespace {
    constexpr uint32_t rgb(const uint32_t r, const uint32_t g, const uint32_t b)
    {
      return r | (g << 8) | (b << 16) | (0xFFu << 24);
    }

    constexpr std::array<uint32_t, fixedScalesMillis.size()> fixedScalesMicros = []
    {
      std::array<uint32_t, fixedScalesMillis.size()> micros{};

      for (std::size_t i = 0; i < micros.size(); ++i)
      {
        micros[i] = static_cast<uint32_t>(fixedScalesMillis[i] * 1000.0 + 0.5);
      }

      return micros;
    }();

    constexpr std::array<DisplayGroup, 7> frameGroups{{
      {"networkIn", "Network in", rgb(0x4E, 0x79, 0xA7)},
      {"input", "Input", rgb(0xF2, 0x8E, 0x2B)},
      {"editorUi", "Editor UI", rgb(0xB0, 0x7A, 0xA1)},
      {"scenePrep", "Scene prep", rgb(0x59, 0xA1, 0x4F)},
      {"renderer", "Renderer", rgb(0xE1, 0x57, 0x59)},
      {"profiler", "Profiler", rgb(0xED, 0xC9, 0x48)},
      {"other", "Other", rgb(0x9C, 0x9C, 0x9C)}
    }};

    constexpr std::array<DisplayGroup, 6> serverGroups{{
      {"scripts", "Scripts", rgb(0xB0, 0x7A, 0xA1)},
      {"physics", "Physics", rgb(0x59, 0xA1, 0x4F)},
      {"collisions", "Collisions", rgb(0xF2, 0x8E, 0x2B)},
      {"replication", "Replication", rgb(0xE1, 0x57, 0x59)},
      {"networkIn", "Network in", rgb(0x4E, 0x79, 0xA7)},
      {"other", "Other", rgb(0x9C, 0x9C, 0x9C)}
    }};

    // The one place scope names map to groups. An empty parent means a top-level scope (parent is the root); "*" means
    // any parent.
    struct Rule {
      std::string_view name;
      std::string_view parent;
      std::string_view group;
    };

    constexpr std::array<Rule, 6> frameRules{{
      {"Network in", "", "networkIn"},
      {"Input", "", "input"},
      {"Editor UI", "", "editorUi"},
      {"Scene prep", "", "scenePrep"},
      {"Renderer", "", "renderer"},
      {"Profiler", "", "profiler"}
    }};

    constexpr std::array<Rule, 7> serverRules{{
      {"Scripts", "Server tick", "scripts"},
      {"Script events", "Server tick", "scripts"},
      {"Physics", "Server tick", "physics"},
      {"Collisions", "Server tick", "collisions"},
      {"Replication", "", "replication"},
      {"Snapshot", "*", "replication"},
      {"Network in", "", "networkIn"}
    }};

    std::span<const Rule> rulesOf(const LaneKind kind)
    {
      if (kind == LaneKind::frame)
      {
        return frameRules;
      }

      return serverRules;
    }

    uint8_t indexOfGroup(const std::span<const DisplayGroup> groups, const std::string_view id)
    {
      for (std::size_t i = 0; i < groups.size(); ++i)
      {
        if (groups[i].id == id)
        {
          return static_cast<uint8_t>(i);
        }
      }

      return static_cast<uint8_t>(groups.size() - 1);
    }

    int64_t sampleEndNanos(const Sample& sample)
    {
      return sample.startNanos + static_cast<int64_t>(sample.wallMicros) * 1000;
    }

    std::size_t clampedWindow(const History& history, const std::size_t window)
    {
      return std::min(window, history.size());
    }

    std::vector<uint32_t> windowWalls(const History& history, const std::size_t window)
    {
      std::vector<uint32_t> walls;
      const auto n = clampedWindow(history, window);

      for (std::size_t i = history.size() - n; i < history.size(); ++i)
      {
        walls.push_back(history.at(i).wallMicros);
      }

      return walls;
    }

    bool ruleMatches(const Rule& rule, const std::span<const ViewNode> nodes, const std::size_t node)
    {
      if (rule.name != nodes[node].name)
      {
        return false;
      }

      const auto parent = static_cast<std::size_t>(nodes[node].parent);

      if (rule.parent == "*")
      {
        return true;
      }

      if (rule.parent.empty())
      {
        return parent == 0;
      }

      return parent < nodes.size() && nodes[parent].name == rule.parent;
    }

    // Appends the subtree's rows depth first; an inactive node stays only to keep an active descendant readable.
    bool appendRows(const std::vector<std::vector<NodeId>>& children, const NodeId node, const std::size_t depth,
                    const std::span<const ViewNode> nodes, const std::vector<BreakdownRow>& byNode,
                    const std::vector<bool>& active, std::vector<BreakdownRow>& rows)
    {
      bool any = false;

      for (const auto child : children[node])
      {
        auto row = byNode[child];
        row.depth = depth;
        row.name = nodes[child].name;
        rows.push_back(std::move(row));

        const auto descendants = appendRows(children, child, depth + 1, nodes, byNode, active, rows);

        if (!active[child] && !descendants)
        {
          rows.pop_back();
        }

        any = any || active[child] || descendants;
      }

      return any;
    }
  }

  std::vector<ViewNode> viewNodes(const ScopeTree& tree)
  {
    std::vector<ViewNode> nodes(tree.size());

    for (std::size_t i = 1; i < nodes.size(); ++i)
    {
      nodes[i].parent = tree.parent(static_cast<NodeId>(i));
      nodes[i].name = std::string(nameOf(tree.name(static_cast<NodeId>(i))));
    }

    return nodes;
  }

  std::span<const DisplayGroup> displayGroups(const LaneKind kind)
  {
    if (kind == LaneKind::frame)
    {
      return frameGroups;
    }

    return serverGroups;
  }

  std::vector<uint8_t> groupOfNodes(const LaneKind kind, const std::span<const ViewNode> nodes)
  {
    const auto groups = displayGroups(kind);
    const auto rules = rulesOf(kind);
    const auto other = static_cast<uint8_t>(groups.size() - 1);
    std::vector<uint8_t> result(nodes.size(), other);

    for (std::size_t i = 1; i < nodes.size(); ++i)
    {
      auto current = i;

      for (std::size_t step = 0; step < nodes.size() && current > 0 && current < nodes.size(); ++step)
      {
        const auto match = std::ranges::find_if(rules, [&](const Rule& rule)
        {
          return ruleMatches(rule, nodes, current);
        });

        if (match != rules.end())
        {
          result[i] = indexOfGroup(groups, match->group);
          break;
        }

        current = static_cast<std::size_t>(nodes[current].parent);
      }
    }

    return result;
  }

  std::array<uint32_t, maxDisplayGroups> groupMicros(const Sample& sample, const std::span<const ViewNode> nodes,
                                                     const std::span<const uint8_t> groups, const uint32_t hiddenMask)
  {
    const auto count = std::min(nodes.size(), maxNodes);
    std::array<uint64_t, maxNodes> childSum{};

    for (std::size_t i = 1; i < count; ++i)
    {
      if (nodes[i].parent < maxNodes)
      {
        childSum[nodes[i].parent] += sample.nodeMicros[i];
      }
    }

    std::array<uint64_t, maxDisplayGroups> totals{};

    for (std::size_t i = 1; i < count && i < groups.size(); ++i)
    {
      if (groups[i] >= maxDisplayGroups || ((hiddenMask >> groups[i]) & 1u) != 0)
      {
        continue;
      }

      const uint64_t micros = sample.nodeMicros[i];
      totals[groups[i]] += micros > childSum[i] ? micros - childSum[i] : 0;
    }

    std::array<uint32_t, maxDisplayGroups> result{};

    for (std::size_t g = 0; g < result.size(); ++g)
    {
      result[g] = static_cast<uint32_t>(std::min<uint64_t>(totals[g], UINT32_MAX));
    }

    return result;
  }

  std::size_t windowSize(const History& history, const double seconds)
  {
    if (history.size() == 0)
    {
      return 0;
    }

    const auto nanos = seconds >= 9.2e9 ? INT64_MAX : (seconds > 0.0 ? static_cast<int64_t>(seconds * 1e9) : 0);
    const auto newestEnd = sampleEndNanos(history.newest());
    const auto cutoff = newestEnd < INT64_MIN + nanos ? INT64_MIN : newestEnd - nanos;
    std::size_t count = 0;

    for (std::size_t i = history.size(); i > 0; --i)
    {
      if (sampleEndNanos(history.at(i - 1)) <= cutoff)
      {
        break;
      }

      ++count;
    }

    return std::max<std::size_t>(count, 1);
  }

  double autoScaleMillis(const uint32_t p99Micros)
  {
    for (std::size_t i = 0; i < fixedScalesMicros.size(); ++i)
    {
      if (p99Micros <= fixedScalesMicros[i])
      {
        return fixedScalesMillis[i];
      }
    }

    return std::ceil(static_cast<double>(p99Micros) / 100000.0) * 100.0;
  }

  std::vector<Column> buildColumns(const History& history, const std::size_t window,
                                   const std::span<const ViewNode> nodes, const std::span<const uint8_t> groups,
                                   const uint32_t hiddenMask, const std::size_t maxColumns)
  {
    const auto n = clampedWindow(history, window);

    if (n == 0 || maxColumns == 0)
    {
      return {};
    }

    const auto median = percentile(windowWalls(history, window), 50.0);
    const auto columnCount = std::min(n, maxColumns);
    const auto base = n / columnCount;
    const auto extra = n % columnCount;
    std::vector<Column> columns(columnCount);
    std::size_t next = history.size() - n;

    for (std::size_t c = 0; c < columnCount; ++c)
    {
      const auto take = c >= columnCount - extra ? base + 1 : base;
      auto& column = columns[c];

      for (std::size_t k = 0; k < take; ++k, ++next)
      {
        const auto& sample = history.at(next);
        const auto micros = groupMicros(sample, nodes, groups, hiddenMask);

        for (std::size_t g = 0; g < maxDisplayGroups; ++g)
        {
          column.groupMicros[g] = std::max(column.groupMicros[g], micros[g]);
        }

        column.wallMicros = std::max(column.wallMicros, sample.wallMicros);
        column.sequence = sample.sequence;
        column.hitch = column.hitch || isHitch(sample.wallMicros, median);
      }
    }

    return columns;
  }

  LaneSummary summarizeLane(const History& history, const std::size_t window)
  {
    LaneSummary summary;
    const auto n = clampedWindow(history, window);

    if (n == 0)
    {
      return summary;
    }

    const auto wall = summarizeWall(history, window);
    summary.averageMillis = wall.average / 1000.0;
    summary.p95Millis = wall.p95 / 1000.0;
    summary.maxMillis = wall.max / 1000.0;
    summary.samples = wall.samples;
    summary.hitches = countHitches(history, window);

    const auto oldestStart = history.at(history.size() - n).startNanos;
    const auto newestEnd = sampleEndNanos(history.newest());
    summary.coveredSeconds = static_cast<double>(newestEnd - oldestStart) / 1e9;

    if (summary.coveredSeconds > 0.0)
    {
      summary.samplesPerSecond = static_cast<double>(n) / summary.coveredSeconds;
    }

    return summary;
  }

  int64_t sumCounter(const History& history, const std::size_t window, const std::size_t counter)
  {
    if (counter >= maxCounters)
    {
      return 0;
    }

    int64_t total = 0;

    for (std::size_t i = history.size() - clampedWindow(history, window); i < history.size(); ++i)
    {
      total += history.at(i).counters[counter];
    }

    return total;
  }

  std::vector<BreakdownRow> breakdownRows(const History& history, const std::span<const ViewNode> nodes,
                                          const std::size_t window, const uint64_t selectedSequence)
  {
    const auto n = clampedWindow(history, window);

    if (n == 0 || nodes.size() < 2)
    {
      return {};
    }

    const auto count = std::min(nodes.size(), maxNodes);
    const Sample* selected = selectedSequence != 0 ? history.findSequence(selectedSequence) : nullptr;
    const auto& reference = selected != nullptr ? *selected : history.newest();
    const auto wallAverage = summarizeWall(history, window).average;

    std::vector<bool> active(count, false);

    for (std::size_t i = history.size() - n; i < history.size(); ++i)
    {
      for (std::size_t node = 1; node < count; ++node)
      {
        active[node] = active[node] || history.at(i).nodeMicros[node] > 0 || history.at(i).nodeCalls[node] > 0;
      }
    }

    std::vector<BreakdownRow> byNode(count);
    std::vector<std::vector<NodeId>> children(count);

    for (std::size_t node = 1; node < count; ++node)
    {
      const auto summary = summarizeNode(history, static_cast<NodeId>(node), window);
      auto& row = byNode[node];
      row.node = static_cast<NodeId>(node);
      row.lastMicros = reference.nodeMicros[node];
      row.averageMicros = summary.average;
      row.p95Micros = summary.p95;
      row.maxMicros = summary.max;
      row.share = wallAverage > 0.0 ? summary.average / wallAverage : 0.0;

      if (nodes[node].parent < count && nodes[node].parent != node)
      {
        children[nodes[node].parent].push_back(static_cast<NodeId>(node));
      }
    }

    std::vector<BreakdownRow> rows;
    appendRows(children, 0, 0, nodes, byNode, active, rows);
    return rows;
  }
}
