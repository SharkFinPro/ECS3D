#include <gtest/gtest.h>

#include "ProfilerView.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {
  using profile::LaneKind;

  struct Spec
  {
    profile::NodeId parent;
    const char* name;
  };

  std::vector<profile::ViewNode> makeNodes(const std::vector<Spec>& specs)
  {
    std::vector<profile::ViewNode> nodes(1);

    for (const auto& spec : specs)
    {
      nodes.push_back({spec.parent, spec.name});
    }

    return nodes;
  }

  // Ids: 1 Network in, 2 Server tick, 3 Scripts, 4 fixedUpdate, 5 Physics, 6 Collisions, 7 Narrow phase,
  // 8 Script events, 9 Replication, 10 State delta, 11 Snapshot, 12 Log forwarding, 13 Physics, 14 Mystery
  std::vector<profile::ViewNode> serverNodes()
  {
    return makeNodes({{0, "Network in"}, {0, "Server tick"}, {2, "Scripts"}, {3, "fixedUpdate"}, {2, "Physics"},
                      {2, "Collisions"}, {6, "Narrow phase"}, {2, "Script events"}, {0, "Replication"},
                      {9, "State delta"}, {0, "Snapshot"}, {0, "Log forwarding"}, {0, "Physics"}, {0, "Mystery"}});
  }

  std::string groupIdOf(const LaneKind kind, const std::vector<profile::ViewNode>& nodes, const std::size_t node)
  {
    const auto groups = profile::groupOfNodes(kind, nodes);
    return std::string(profile::displayGroups(kind)[groups.at(node)].id);
  }

  profile::Sample sampleAt(const uint64_t sequence, const int64_t startNanos, const uint32_t wallMicros)
  {
    profile::Sample sample;
    sample.sequence = sequence;
    sample.startNanos = startNanos;
    sample.wallMicros = wallMicros;
    return sample;
  }
}

TEST(ProfilerView, ViewNodesMirrorsTheScopeTree)
{
  profile::ScopeTree tree;
  const auto a = tree.child(0, profile::internName("ViewTestA")).value();
  const auto b = tree.child(a, profile::internName("ViewTestB")).value();

  const auto nodes = profile::viewNodes(tree);

  ASSERT_EQ(nodes.size(), 3u);
  EXPECT_TRUE(nodes[0].name.empty());
  EXPECT_EQ(nodes[a].name, "ViewTestA");
  EXPECT_EQ(nodes[a].parent, 0u);
  EXPECT_EQ(nodes[b].name, "ViewTestB");
  EXPECT_EQ(nodes[b].parent, a);
}

TEST(ProfilerView, ServerScopesLandInTheirGroups)
{
  const auto nodes = serverNodes();

  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 1), "networkIn");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 3), "scripts");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 4), "scripts");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 5), "physics");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 6), "collisions");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 7), "collisions");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 8), "scripts");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 9), "replication");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 10), "replication");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 11), "replication");
}

TEST(ProfilerView, ServerUnmatchedScopesAreOther)
{
  const auto nodes = serverNodes();

  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 0), "other");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 2), "other");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 12), "other");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 14), "other");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 13), "other");
}

TEST(ProfilerView, FrameScopesLandInTheirGroups)
{
  const auto nodes = makeNodes({{0, "Network in"}, {1, "Snapshot apply"}, {0, "Input"}, {0, "Editor UI"},
                                {0, "Scene prep"}, {0, "Renderer"}, {0, "Profiler"}, {0, "Mystery"}});

  EXPECT_EQ(groupIdOf(LaneKind::frame, nodes, 1), "networkIn");
  EXPECT_EQ(groupIdOf(LaneKind::frame, nodes, 2), "networkIn");
  EXPECT_EQ(groupIdOf(LaneKind::frame, nodes, 3), "input");
  EXPECT_EQ(groupIdOf(LaneKind::frame, nodes, 4), "editorUi");
  EXPECT_EQ(groupIdOf(LaneKind::frame, nodes, 5), "scenePrep");
  EXPECT_EQ(groupIdOf(LaneKind::frame, nodes, 6), "renderer");
  EXPECT_EQ(groupIdOf(LaneKind::frame, nodes, 7), "profiler");
  EXPECT_EQ(groupIdOf(LaneKind::frame, nodes, 8), "other");
  EXPECT_EQ(groupIdOf(LaneKind::frame, nodes, 0), "other");
}

TEST(ProfilerView, DisplayGroupsAreWellFormed)
{
  for (const auto kind : {LaneKind::frame, LaneKind::server})
  {
    const auto groups = profile::displayGroups(kind);
    std::set<std::string_view> ids;
    std::set<uint32_t> colors;

    ASSERT_FALSE(groups.empty());
    EXPECT_LE(groups.size(), profile::maxDisplayGroups);
    EXPECT_EQ(groups.back().id, "other");

    for (const auto& group : groups)
    {
      ids.insert(group.id);
      colors.insert(group.color);
      EXPECT_EQ(group.color >> 24, 0xFFu);
    }

    EXPECT_EQ(ids.size(), groups.size());
    EXPECT_EQ(colors.size(), groups.size());
  }
}

TEST(ProfilerView, GroupMicrosIsExclusiveTime)
{
  const auto nodes = makeNodes({{0, "Server tick"}, {1, "Physics"}});
  const auto groups = profile::groupOfNodes(LaneKind::server, nodes);
  profile::Sample sample;
  sample.nodeMicros[1] = 10000;
  sample.nodeMicros[2] = 6000;

  const auto micros = profile::groupMicros(sample, nodes, groups, 0);

  EXPECT_EQ(micros[0], 0u);
  EXPECT_EQ(micros[1], 6000u);
  EXPECT_EQ(micros[5], 4000u);
}

TEST(ProfilerView, GroupMicrosClampsWhenChildrenExceedTheParent)
{
  const auto nodes = makeNodes({{0, "Server tick"}, {1, "Physics"}, {1, "Scripts"}});
  const auto groups = profile::groupOfNodes(LaneKind::server, nodes);
  profile::Sample sample;
  sample.nodeMicros[1] = 1000;
  sample.nodeMicros[2] = 800;
  sample.nodeMicros[3] = 700;

  const auto micros = profile::groupMicros(sample, nodes, groups, 0);

  EXPECT_EQ(micros[5], 0u);
  EXPECT_EQ(micros[1], 800u);
  EXPECT_EQ(micros[0], 700u);
}

TEST(ProfilerView, GroupMicrosHidesGroupsByMask)
{
  const auto nodes = makeNodes({{0, "Server tick"}, {1, "Physics"}});
  const auto groups = profile::groupOfNodes(LaneKind::server, nodes);
  profile::Sample sample;
  sample.nodeMicros[1] = 10000;
  sample.nodeMicros[2] = 6000;

  EXPECT_EQ(profile::groupMicros(sample, nodes, groups, 0)[1], 6000u);

  const auto hidden = profile::groupMicros(sample, nodes, groups, 1u << 1);

  EXPECT_EQ(hidden[1], 0u);
  EXPECT_EQ(hidden[5], 4000u);
}

TEST(ProfilerView, GroupMicrosIgnoresNodesBeyondTheTable)
{
  const auto nodes = makeNodes({{0, "Physics"}});
  const auto groups = profile::groupOfNodes(LaneKind::server, nodes);
  profile::Sample sample;
  sample.nodeMicros[1] = 500;
  sample.nodeMicros[2] = 900;
  sample.nodeMicros[profile::maxNodes - 1] = 900;

  const auto micros = profile::groupMicros(sample, nodes, groups, 0);

  EXPECT_EQ(micros[5], 500u);
  EXPECT_EQ(std::count(micros.begin(), micros.end(), 900u), 0);
}

TEST(ProfilerView, WindowSizeOfNothingIsZero)
{
  profile::History history(8);

  EXPECT_EQ(profile::windowSize(history, 5.0), 0u);
}

TEST(ProfilerView, WindowSizeCountsSamplesEndingInsideTheWindow)
{
  profile::History history(8);

  // Ends at 1.0 s, 2.0 s, 3.0 s, 4.0 s (each sample is 1 ms wall; starts are 1 ms earlier).
  for (uint64_t i = 1; i <= 4; ++i)
  {
    history.push(sampleAt(i, static_cast<int64_t>(i) * 1000000000 - 1000000, 1000));
  }

  EXPECT_EQ(profile::windowSize(history, 1.5), 2u);
  EXPECT_EQ(profile::windowSize(history, 2.0), 2u);
  EXPECT_EQ(profile::windowSize(history, 2.1), 3u);
  EXPECT_EQ(profile::windowSize(history, 100.0), 4u);
  EXPECT_EQ(profile::windowSize(history, 0.0), 1u);
}

TEST(ProfilerView, AutoScalePicksTheSmallestFixedScaleThatFits)
{
  EXPECT_DOUBLE_EQ(profile::autoScaleMillis(0), 16.7);
  EXPECT_DOUBLE_EQ(profile::autoScaleMillis(16700), 16.7);
  EXPECT_DOUBLE_EQ(profile::autoScaleMillis(16701), 33.3);
  EXPECT_DOUBLE_EQ(profile::autoScaleMillis(50000), 50.0);
  EXPECT_DOUBLE_EQ(profile::autoScaleMillis(100000), 100.0);
  EXPECT_DOUBLE_EQ(profile::autoScaleMillis(100001), 200.0);
  EXPECT_DOUBLE_EQ(profile::autoScaleMillis(250000), 300.0);
}

TEST(ProfilerView, BuildColumnsGivesOnePerSampleWhenTheyFit)
{
  const auto nodes = makeNodes({{0, "Physics"}});
  const auto groups = profile::groupOfNodes(LaneKind::server, nodes);
  profile::History history(8);

  for (uint64_t i = 1; i <= 3; ++i)
  {
    auto sample = sampleAt(i, 0, 1000);
    sample.nodeMicros[1] = static_cast<uint32_t>(i * 100);
    history.push(sample);
  }

  const auto columns = profile::buildColumns(history, 10, nodes, groups, 0, 5);

  ASSERT_EQ(columns.size(), 3u);
  EXPECT_EQ(columns[0].sequence, 1u);
  EXPECT_EQ(columns[2].sequence, 3u);
  EXPECT_EQ(columns[1].groupMicros[5], 200u);
  EXPECT_EQ(columns[1].wallMicros, 1000u);
}

TEST(ProfilerView, BuildColumnsKeepsASpikeWhenMerging)
{
  const auto nodes = makeNodes({{0, "Physics"}});
  const auto groups = profile::groupOfNodes(LaneKind::server, nodes);
  profile::History history(16);

  for (uint64_t i = 1; i <= 10; ++i)
  {
    auto sample = sampleAt(i, 0, i == 3 ? 30000 : 1000);
    sample.nodeMicros[1] = i == 3 ? 20000 : 500;
    history.push(sample);
  }

  const auto columns = profile::buildColumns(history, 10, nodes, groups, 0, 5);

  ASSERT_EQ(columns.size(), 5u);
  EXPECT_EQ(columns[1].groupMicros[5], 20000u);
  EXPECT_EQ(columns[1].wallMicros, 30000u);
  EXPECT_TRUE(columns[1].hitch);
  EXPECT_EQ(columns[1].sequence, 4u);
  EXPECT_EQ(columns[0].groupMicros[5], 500u);
  EXPECT_FALSE(columns[0].hitch);
  EXPECT_EQ(columns[4].sequence, 10u);
}

TEST(ProfilerView, BuildColumnsGivesTheExtraSamplesToTheNewestColumns)
{
  profile::History history(16);

  for (uint64_t i = 1; i <= 7; ++i)
  {
    history.push(sampleAt(i, 0, 1000));
  }

  const auto columns = profile::buildColumns(history, 7, {}, {}, 0, 3);

  ASSERT_EQ(columns.size(), 3u);
  EXPECT_EQ(columns[0].sequence, 2u);
  EXPECT_EQ(columns[1].sequence, 4u);
  EXPECT_EQ(columns[2].sequence, 7u);
}

TEST(ProfilerView, BuildColumnsUsesOnlyTheNewestWindow)
{
  profile::History history(16);

  for (uint64_t i = 1; i <= 6; ++i)
  {
    history.push(sampleAt(i, 0, 1000));
  }

  const auto columns = profile::buildColumns(history, 2, {}, {}, 0, 10);

  ASSERT_EQ(columns.size(), 2u);
  EXPECT_EQ(columns[0].sequence, 5u);
  EXPECT_EQ(columns[1].sequence, 6u);
}

TEST(ProfilerView, BuildColumnsHandlesEmptyRequests)
{
  profile::History history(4);
  history.push(sampleAt(1, 0, 1000));

  EXPECT_EQ(profile::buildColumns(history, 4, {}, {}, 0, 1).size(), 1u);
  EXPECT_TRUE(profile::buildColumns(history, 4, {}, {}, 0, 0).empty());
  EXPECT_TRUE(profile::buildColumns(history, 0, {}, {}, 0, 4).empty());
}

TEST(ProfilerView, SummarizeLaneReportsKnownValues)
{
  profile::History history(8);

  // 1 ms, 2 ms, 3 ms and a 10 ms hitch, starting every 100 ms.
  const uint32_t walls[] = {1000, 2000, 3000, 10000};

  for (uint64_t i = 0; i < 4; ++i)
  {
    history.push(sampleAt(i + 1, static_cast<int64_t>(i) * 100000000, walls[i]));
  }

  const auto summary = profile::summarizeLane(history, 10);

  EXPECT_EQ(summary.samples, 4u);
  EXPECT_DOUBLE_EQ(summary.averageMillis, 4.0);
  EXPECT_DOUBLE_EQ(summary.maxMillis, 10.0);
  EXPECT_DOUBLE_EQ(summary.p95Millis, 10.0);
  EXPECT_EQ(summary.hitches, 1u);
  EXPECT_NEAR(summary.coveredSeconds, 0.31, 1e-9);
  EXPECT_NEAR(summary.samplesPerSecond, 4.0 / 0.31, 1e-6);
}

TEST(ProfilerView, SummarizeLaneHasNoRateWithoutCoveredTime)
{
  profile::History history(4);

  EXPECT_EQ(profile::summarizeLane(history, 4).samples, 0u);

  history.push(sampleAt(1, 0, 0));
  const auto summary = profile::summarizeLane(history, 4);

  EXPECT_EQ(summary.samples, 1u);
  EXPECT_DOUBLE_EQ(summary.coveredSeconds, 0.0);
  EXPECT_DOUBLE_EQ(summary.samplesPerSecond, 0.0);
}

TEST(ProfilerView, SumCounterAddsTheWindow)
{
  profile::History history(8);

  for (uint64_t i = 1; i <= 4; ++i)
  {
    auto sample = sampleAt(i, 0, 1000);
    sample.counters[2] = static_cast<int64_t>(i);
    history.push(sample);
  }

  EXPECT_EQ(profile::sumCounter(history, 10, 2), 10);
  EXPECT_EQ(profile::sumCounter(history, 2, 2), 7);
  EXPECT_EQ(profile::sumCounter(history, 10, 1), 0);
  EXPECT_EQ(profile::sumCounter(history, 10, profile::maxCounters), 0);
}

namespace {
  // Ids: 1 Server tick, 2 Scripts, 3 Idle, 4 Physics
  profile::History breakdownHistory()
  {
    profile::History history(8);

    for (uint64_t i = 1; i <= 2; ++i)
    {
      auto sample = sampleAt(i, 0, 10000);
      sample.nodeMicros[1] = 8000 * static_cast<uint32_t>(i);
      sample.nodeMicros[2] = 2000 * static_cast<uint32_t>(i);
      sample.nodeMicros[4] = 1000;
      sample.nodeCalls[1] = 1;
      sample.nodeCalls[2] = 1;
      sample.nodeCalls[4] = 1;
      history.push(sample);
    }

    return history;
  }
}

TEST(ProfilerView, BreakdownRowsAreDepthFirstAndSkipInactiveNodes)
{
  const auto nodes = makeNodes({{0, "Server tick"}, {1, "Scripts"}, {1, "Idle"}, {0, "Physics"}});

  const auto rows = profile::breakdownRows(breakdownHistory(), nodes, 10, 0);

  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[0].name, "Server tick");
  EXPECT_EQ(rows[0].depth, 0u);
  EXPECT_EQ(rows[1].name, "Scripts");
  EXPECT_EQ(rows[1].depth, 1u);
  EXPECT_EQ(rows[2].name, "Physics");
  EXPECT_EQ(rows[2].depth, 0u);
  EXPECT_EQ(rows[2].node, 4u);
}

TEST(ProfilerView, BreakdownRowsCarryStatisticsAndShare)
{
  const auto nodes = makeNodes({{0, "Server tick"}, {1, "Scripts"}, {1, "Idle"}, {0, "Physics"}});

  const auto rows = profile::breakdownRows(breakdownHistory(), nodes, 10, 0);

  EXPECT_DOUBLE_EQ(rows[0].averageMicros, 12000.0);
  EXPECT_EQ(rows[0].maxMicros, 16000u);
  EXPECT_EQ(rows[0].p95Micros, 16000u);
  EXPECT_DOUBLE_EQ(rows[0].share, 1.2);
  EXPECT_DOUBLE_EQ(rows[2].share, 0.1);
  EXPECT_EQ(rows[0].lastMicros, 16000u);
}

TEST(ProfilerView, BreakdownRowsFollowTheSelectedSample)
{
  const auto nodes = makeNodes({{0, "Server tick"}, {1, "Scripts"}, {1, "Idle"}, {0, "Physics"}});
  const auto history = breakdownHistory();

  EXPECT_EQ(profile::breakdownRows(history, nodes, 10, 1)[0].lastMicros, 8000u);
  EXPECT_EQ(profile::breakdownRows(history, nodes, 10, 999)[0].lastMicros, 16000u);
}

TEST(ProfilerView, BreakdownRowsOfNothingAreEmpty)
{
  const auto nodes = makeNodes({{0, "Server tick"}});

  EXPECT_TRUE(profile::breakdownRows(profile::History(4), nodes, 10, 0).empty());
}

TEST(ProfilerView, SnapshotIsReplicationAtAnyDepth)
{
  // 1 Script reload check, 2 Snapshot, 3 Network in, 4 Snapshot, 5 Child (under Snapshot)
  const auto nodes = makeNodes({{0, "Script reload check"}, {1, "Snapshot"}, {0, "Network in"}, {3, "Snapshot"},
                                {4, "Child"}});

  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 1), "other");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 2), "replication");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 3), "networkIn");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 4), "replication");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 5), "replication");
}

TEST(ProfilerView, TopLevelRulesNeedTheRootAsParent)
{
  // 1 Network in past the table, 2 Physics under it, 3 Snapshot under it, 4 Scripts under a valid Server tick
  const auto nodes = makeNodes({{9, "Network in"}, {1, "Physics"}, {1, "Snapshot"}, {5, "Scripts"}, {0, "Server tick"}});

  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 1), "other");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 2), "other");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 3), "replication");
  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 4), "scripts");
}

TEST(ProfilerView, MalformedNodeTablesTerminate)
{
  // 1<->2 cycle, 3 its own parent, 4 parent past the table, 5 a valid positive control
  const auto nodes = makeNodes({{2, "Network in"}, {1, "Network in"}, {3, "Network in"}, {99, "Network in"},
                                {0, "Network in"}});
  const auto groups = profile::groupOfNodes(LaneKind::server, nodes);

  ASSERT_EQ(groups.size(), nodes.size());

  for (std::size_t node = 0; node < 5; ++node)
  {
    EXPECT_EQ(groupIdOf(LaneKind::server, nodes, node), "other") << node;
  }

  EXPECT_EQ(groupIdOf(LaneKind::server, nodes, 5), "networkIn");

  profile::History history(4);
  auto sample = sampleAt(1, 0, 1000);

  for (std::size_t node = 1; node < nodes.size(); ++node)
  {
    sample.nodeMicros[node] = 100;
    sample.nodeCalls[node] = 1;
  }

  history.push(sample);

  const auto rows = profile::breakdownRows(history, nodes, 4, 0);

  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].node, 5u);
}

TEST(ProfilerView, GroupMicrosIgnoresExtraNodesPastMaxNodes)
{
  std::vector<Spec> specs(199, Spec{0, "Network in"});
  const auto nodes = makeNodes(specs);
  const auto groups = profile::groupOfNodes(LaneKind::server, nodes);
  profile::Sample sample;

  for (std::size_t node = 1; node < profile::maxNodes; ++node)
  {
    sample.nodeMicros[node] = 1;
  }

  // Aliases nodeMicros[maxNodes] if the bound were missing.
  sample.nodeCalls[0] = 0xFFFF;
  sample.nodeCalls[1] = 0xFFFF;

  ASSERT_EQ(nodes.size(), 200u);
  ASSERT_EQ(groups.size(), 200u);
  EXPECT_EQ(profile::groupMicros(sample, nodes, groups, 0)[4], profile::maxNodes - 1);
}

TEST(ProfilerView, GroupMicrosSaturatesAGroupSum)
{
  const auto nodes = makeNodes({{0, "Network in"}, {0, "Network in"}});
  const auto groups = profile::groupOfNodes(LaneKind::server, nodes);
  profile::Sample sample;
  sample.nodeMicros[1] = 4000000000u;

  EXPECT_EQ(profile::groupMicros(sample, nodes, groups, 0)[4], 4000000000u);

  sample.nodeMicros[2] = 4000000000u;

  EXPECT_EQ(profile::groupMicros(sample, nodes, groups, 0)[4], std::numeric_limits<uint32_t>::max());
}

TEST(ProfilerView, GroupMicrosHidesSeveralGroupsIncludingTheFirst)
{
  const auto nodes = makeNodes({{0, "Server tick"}, {1, "Scripts"}, {1, "Physics"}, {1, "Collisions"}});
  const auto groups = profile::groupOfNodes(LaneKind::server, nodes);
  profile::Sample sample;
  sample.nodeMicros[2] = 100;
  sample.nodeMicros[3] = 200;
  sample.nodeMicros[4] = 300;

  const auto all = profile::groupMicros(sample, nodes, groups, 0);
  const auto hidden = profile::groupMicros(sample, nodes, groups, 0b11u);

  EXPECT_EQ(all[0], 100u);
  EXPECT_EQ(all[1], 200u);
  EXPECT_EQ(hidden[0], 0u);
  EXPECT_EQ(hidden[1], 0u);
  EXPECT_EQ(hidden[2], 300u);
}

TEST(ProfilerView, WindowSizeTreatsNegativeAndNanSecondsAsZero)
{
  profile::History history(8);

  for (uint64_t i = 1; i <= 4; ++i)
  {
    history.push(sampleAt(i, static_cast<int64_t>(i) * 1000000000 - 1000000, 1000));
  }

  EXPECT_EQ(profile::windowSize(history, -5.0), 1u);
  EXPECT_EQ(profile::windowSize(history, std::nan("")), 1u);
  EXPECT_EQ(profile::windowSize(history, 1e300), 4u);
  EXPECT_EQ(profile::windowSize(history, 3.5), 4u);
}

TEST(ProfilerView, BreakdownRowsOmitANodeActiveOnlyBeforeTheWindow)
{
  const auto nodes = makeNodes({{0, "Server tick"}, {0, "Physics"}});
  profile::History history(8);

  for (uint64_t i = 1; i <= 3; ++i)
  {
    auto sample = sampleAt(i, 0, 1000);
    sample.nodeMicros[1] = 500;
    sample.nodeCalls[1] = 1;
    sample.nodeMicros[2] = i == 1 ? 400 : 0;
    sample.nodeCalls[2] = i == 1 ? 1 : 0;
    history.push(sample);
  }

  const auto narrow = profile::breakdownRows(history, nodes, 2, 0);
  const auto wide = profile::breakdownRows(history, nodes, 3, 0);

  ASSERT_EQ(narrow.size(), 1u);
  EXPECT_EQ(narrow[0].name, "Server tick");
  ASSERT_EQ(wide.size(), 2u);
  EXPECT_EQ(wide[1].name, "Physics");
}

TEST(ProfilerView, BreakdownShareIsZeroWithoutWallTime)
{
  const auto nodes = makeNodes({{0, "Server tick"}});
  profile::History history(4);
  auto sample = sampleAt(1, 0, 0);
  sample.nodeMicros[1] = 500;
  history.push(sample);

  const auto rows = profile::breakdownRows(history, nodes, 4, 0);

  ASSERT_EQ(rows.size(), 1u);
  EXPECT_DOUBLE_EQ(rows[0].averageMicros, 500.0);
  EXPECT_DOUBLE_EQ(rows[0].share, 0.0);
}

TEST(ProfilerView, BreakdownKeepsAnInactiveParentOfAnActiveChild)
{
  const auto nodes = makeNodes({{0, "Server tick"}, {1, "Scripts"}, {0, "Idle"}});
  profile::History history(4);
  auto sample = sampleAt(1, 0, 1000);
  sample.nodeMicros[2] = 300;
  sample.nodeCalls[2] = 1;
  history.push(sample);

  const auto rows = profile::breakdownRows(history, nodes, 4, 0);

  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].name, "Server tick");
  EXPECT_EQ(rows[0].depth, 0u);
  EXPECT_DOUBLE_EQ(rows[0].averageMicros, 0.0);
  EXPECT_EQ(rows[0].maxMicros, 0u);
  EXPECT_EQ(rows[1].name, "Scripts");
  EXPECT_EQ(rows[1].depth, 1u);
  EXPECT_DOUBLE_EQ(rows[1].averageMicros, 300.0);
}
