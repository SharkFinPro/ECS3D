#ifndef PROFILERVIEW_H
#define PROFILERVIEW_H

#include "History.h"
#include "ScopeTree.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace profile {
  // A lane's scope tree as plain names, so a local ScopeTree and a remote node table read the same way.
  // The index is the node id; index 0 is the root and has an empty name.
  struct ViewNode {
    NodeId parent = 0;
    std::string name;
  };

  [[nodiscard]] std::vector<ViewNode> viewNodes(const ScopeTree& tree);

  // An editor or client frame lane, or a server tick lane.
  enum class LaneKind : uint8_t { frame, server };

  inline constexpr std::size_t maxDisplayGroups = 8;

  struct DisplayGroup {
    std::string_view id;
    std::string_view label;
    uint32_t color;   // R | G << 8 | B << 16 | A << 24, fully opaque
  };

  // Fixed per lane kind, bottom of the stack first; the last entry is always "Other".
  [[nodiscard]] std::span<const DisplayGroup> displayGroups(LaneKind kind);

  // The group of every node, indexed like nodes; the root gets Other. A node takes the group of its nearest
  // ancestor-or-self whose name maps to one.
  [[nodiscard]] std::vector<uint8_t> groupOfNodes(LaneKind kind, std::span<const ViewNode> nodes);

  // Exclusive time: a node's micros minus its direct children's (clamped at 0), summed into its group. Hidden groups
  // (bit i of hiddenMask is group i) read 0. Nodes at or past nodes.size() or maxNodes are ignored.
  [[nodiscard]] std::array<uint32_t, maxDisplayGroups> groupMicros(const Sample& sample,
                                                                   std::span<const ViewNode> nodes,
                                                                   std::span<const uint8_t> groups,
                                                                   uint32_t hiddenMask);

  // How many of the newest samples end within the last `seconds` of the newest sample's end. At least 1 when the
  // history is not empty, 0 when it is.
  [[nodiscard]] std::size_t windowSize(const History& history, double seconds);

  inline constexpr std::array<double, 4> fixedScalesMillis{16.7, 33.3, 50.0, 100.0};

  // The smallest fixed scale at or above p99; above 100 ms, p99 rounded up to the next whole 100 ms.
  [[nodiscard]] double autoScaleMillis(uint32_t p99Micros);

  struct Column {
    std::array<uint32_t, maxDisplayGroups> groupMicros{};
    uint32_t wallMicros = 0;
    uint64_t sequence = 0;   // the newest sample merged into this column
    bool hitch = false;
  };

  // One column per sample of the newest `window` samples, oldest first. Beyond maxColumns, adjacent samples merge into
  // exactly maxColumns columns (extra samples go to the newest columns) by max per group and wall with hitch OR'ed, so
  // a spike survives. A sample is a hitch against the window's median wall.
  [[nodiscard]] std::vector<Column> buildColumns(const History& history, std::size_t window,
                                                 std::span<const ViewNode> nodes, std::span<const uint8_t> groups,
                                                 uint32_t hiddenMask, std::size_t maxColumns);

  struct LaneSummary {
    double averageMillis = 0.0;
    double p95Millis = 0.0;
    double maxMillis = 0.0;
    double samplesPerSecond = 0.0;   // 0 when nothing is covered
    std::size_t hitches = 0;
    std::size_t samples = 0;
    double coveredSeconds = 0.0;     // newest end - oldest start in the window
  };

  [[nodiscard]] LaneSummary summarizeLane(const History& history, std::size_t window);

  // Sum of counter slot `counter` over the window; an out-of-range slot gives 0.
  [[nodiscard]] int64_t sumCounter(const History& history, std::size_t window, std::size_t counter);

  struct BreakdownRow {
    NodeId node = 0;
    std::size_t depth = 0;       // top-level nodes are 0
    std::string_view name;       // points into the nodes span the caller passed
    uint32_t lastMicros = 0;     // the selected sample's value, or the newest sample's
    double averageMicros = 0.0;
    uint32_t p95Micros = 0;
    uint32_t maxMicros = 0;
    double share = 0.0;          // averageMicros / the window's average wall, 0 when that is 0
  };

  // Depth-first, children in node-id order, root excluded; only nodes with time or calls in the window.
  // selectedSequence 0 means none; a sequence no longer in the history falls back to the newest.
  [[nodiscard]] std::vector<BreakdownRow> breakdownRows(const History& history, std::span<const ViewNode> nodes,
                                                        std::size_t window, uint64_t selectedSequence);
}

#endif
