#include <gtest/gtest.h>

#include <ProfilerCapture.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
  using Clock = std::chrono::system_clock;

  Clock::time_point at(const int64_t epochSeconds)
  {
    return Clock::time_point(std::chrono::seconds(epochSeconds));
  }

  const std::string expectedHeader =
    "lane,sequence,wallMicros,frame.networkIn,frame.input,frame.editorUi,frame.scenePrep,frame.renderer,"
    "frame.profiler,frame.other,server.scripts,server.physics,server.collisions,server.replication,"
    "server.networkIn,server.other,ticks,droppedSteps\n";

  // 1 Network in, 2 Input, 3 Renderer, 4 a child of Renderer.
  std::vector<profile::ViewNode> editorNodes()
  {
    return {{}, {0, "Network in"}, {0, "Input"}, {0, "Renderer"}, {3, "Present"}};
  }

  // 1 Server tick, 2 Scripts, 3 Physics.
  std::vector<profile::ViewNode> serverNodes()
  {
    return {{}, {0, "Server tick"}, {1, "Scripts"}, {1, "Physics"}};
  }

  profile::Sample editorSample(const uint64_t sequence)
  {
    profile::Sample sample;
    sample.sequence = sequence;
    sample.wallMicros = 700;
    sample.nodeMicros[1] = 100;
    sample.nodeMicros[2] = 50;
    sample.nodeMicros[3] = 400;
    sample.nodeMicros[4] = 150;
    return sample;
  }

  profile::Sample serverSample(const uint64_t sequence)
  {
    profile::Sample sample;
    sample.sequence = sequence;
    sample.wallMicros = 1100;
    sample.nodeMicros[1] = 1000;
    sample.nodeMicros[2] = 300;
    sample.nodeMicros[3] = 200;
    sample.counters[0] = 2;
    sample.counters[1] = 1;
    return sample;
  }

  struct Histories {
    profile::History editor{16};
    profile::History server{16};
  };

  CaptureInput inputFor(const Histories& histories, const std::size_t editorWindow = 16,
                        const std::size_t serverWindow = 16)
  {
    CaptureInput input;
    input.app = "editor";
    input.engineVersion = "1.2.3";
    input.buildType = "Release";
    input.platform = "Windows";
    input.sceneName = "Arena";
    input.objectCount = 42;
    input.connectionCount = 2;
    input.lanes.push_back({"editor", profile::LaneKind::frame, &histories.editor, editorNodes(), editorWindow,
                           std::nullopt, std::nullopt});
    input.lanes.push_back({"server", profile::LaneKind::server, &histories.server, serverNodes(), serverWindow,
                           std::size_t{0}, std::size_t{1}});
    return input;
  }

  std::vector<std::string> lines(const std::string& text)
  {
    std::vector<std::string> result;
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line))
    {
      result.push_back(line);
    }

    return result;
  }

  void collectStrings(const nlohmann::json& value, std::set<std::string>& out)
  {
    if (value.is_string())
    {
      out.insert(value.get<std::string>());
    }
    else if (value.is_array() || value.is_object())
    {
      for (const auto& child : value)
      {
        collectStrings(child, out);
      }
    }
  }

  class ProfilerCaptureFiles : public testing::Test {
  protected:
    void SetUp() override
    {
      const auto* info = testing::UnitTest::GetInstance()->current_test_info();

      m_directory = std::filesystem::temp_directory_path() /
                    ("ecs3d-" + std::string(info->test_suite_name()) + "-" + std::string(info->name()));

      std::error_code error;
      std::filesystem::remove_all(m_directory, error);
    }

    void TearDown() override
    {
      std::error_code error;
      std::filesystem::remove_all(m_directory, error);
    }

    std::filesystem::path m_directory;
  };
}

TEST(ProfilerCapture, HeaderIsPinned)
{
  Histories histories;

  EXPECT_EQ(captureCsv(inputFor(histories)), expectedHeader);
}

TEST(ProfilerCapture, EditorRowFillsFrameColumnsAndLeavesServerOnesEmpty)
{
  Histories histories;
  histories.editor.push(editorSample(7));

  const auto rows = lines(captureCsv(inputFor(histories)));

  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0] + "\n", expectedHeader);
  EXPECT_EQ(rows[1], "editor,7,700,100,50,0,0,400,0,0,,,,,,,,");
}

TEST(ProfilerCapture, ServerRowFillsServerColumnsAndCounters)
{
  Histories histories;
  histories.server.push(serverSample(3));

  const auto rows = lines(captureCsv(inputFor(histories)));

  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[1], "server,3,1100,,,,,,,,300,200,0,0,0,500,2,1");
}

TEST(ProfilerCapture, EditorRowsComeBeforeServerRowsAndEachLaneIsOldestFirst)
{
  Histories histories;
  histories.editor.push(editorSample(1));
  histories.editor.push(editorSample(2));
  histories.server.push(serverSample(10));
  histories.server.push(serverSample(11));

  const auto rows = lines(captureCsv(inputFor(histories)));

  ASSERT_EQ(rows.size(), 5u);
  EXPECT_EQ(rows[1].substr(0, 9), "editor,1,");
  EXPECT_EQ(rows[2].substr(0, 9), "editor,2,");
  EXPECT_EQ(rows[3].substr(0, 10), "server,10,");
  EXPECT_EQ(rows[4].substr(0, 10), "server,11,");
}

TEST(ProfilerCapture, WindowKeepsTheNewestSamples)
{
  Histories histories;

  for (uint64_t sequence = 1; sequence <= 4; ++sequence)
  {
    histories.editor.push(editorSample(sequence));
  }

  const auto rows = lines(captureCsv(inputFor(histories, 2)));

  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[1].substr(0, 9), "editor,3,");
  EXPECT_EQ(rows[2].substr(0, 9), "editor,4,");
}

TEST(ProfilerCapture, MissingServerHistoryWritesOnlyEditorRows)
{
  Histories histories;
  histories.editor.push(editorSample(1));

  auto input = inputFor(histories);
  input.lanes[1].history = nullptr;

  const auto csv = captureCsv(input);

  EXPECT_EQ(lines(csv).size(), 2u);
  EXPECT_EQ(csv.find("server,"), std::string::npos);
  EXPECT_EQ(csv.find('\r'), std::string::npos);
}

TEST(ProfilerCapture, CsvFieldQuotesOnlyWhenNeeded)
{
  EXPECT_EQ(csvField("editor"), "editor");
  EXPECT_EQ(csvField("a,b"), "\"a,b\"");
  EXPECT_EQ(csvField("say \"hi\""), "\"say \"\"hi\"\"\"");
  EXPECT_EQ(csvField("one\ntwo"), "\"one\ntwo\"");
  EXPECT_EQ(csvField("one\rtwo"), "\"one\rtwo\"");
  EXPECT_EQ(csvField(""), "");
}

TEST(ProfilerCapture, LaneNameIsQuotedInRows)
{
  Histories histories;
  histories.editor.push(editorSample(1));

  auto input = inputFor(histories);
  input.lanes[0].name = "ed,itor";

  EXPECT_EQ(lines(captureCsv(input))[1].substr(0, 11), "\"ed,itor\",1");
}

TEST(ProfilerCapture, OutputIsIdenticalUnderACommaDecimalLocale)
{
  Histories histories;
  histories.editor.push(editorSample(1));
  histories.server.push(serverSample(2));

  auto input = inputFor(histories);
  input.capturedAt = "2026-01-04T04:05:06Z";
  input.snapshots.push_back({12.5, 2048, "join", 1.25, 3});

  const auto csv = captureCsv(input);
  const auto sidecar = captureSidecar(input).dump();

  std::locale previous;

  try
  {
    previous = std::locale::global(std::locale("de_DE.UTF-8"));
  }
  catch (const std::runtime_error&)
  {
    try
    {
      previous = std::locale::global(std::locale("German_Germany.1252"));
    }
    catch (const std::runtime_error&)
    {
      GTEST_SKIP() << "no locale with a comma decimal separator is installed";
    }
  }

  const auto csvUnderLocale = captureCsv(input);
  const auto sidecarUnderLocale = captureSidecar(input).dump();
  std::locale::global(previous);

  EXPECT_EQ(csvUnderLocale, csv);
  EXPECT_EQ(sidecarUnderLocale, sidecar);
  EXPECT_NE(sidecar.find("12.5"), std::string::npos);
}

TEST(ProfilerCapture, SidecarHasEveryRequiredKey)
{
  Histories histories;
  histories.editor.push(editorSample(1));
  histories.editor.push(editorSample(2));

  auto input = inputFor(histories);
  input.capturedAt = "2026-01-04T04:05:06Z";
  const auto sidecar = captureSidecar(input);

  EXPECT_EQ(sidecar.at("engineVersion"), "1.2.3");
  EXPECT_EQ(sidecar.at("buildType"), "Release");
  EXPECT_EQ(sidecar.at("platform"), "Windows");
  EXPECT_EQ(sidecar.at("app"), "editor");
  EXPECT_EQ(sidecar.at("sceneName"), "Arena");
  EXPECT_EQ(sidecar.at("objectCount"), 42);
  EXPECT_EQ(sidecar.at("connectionCount"), 2);
  EXPECT_EQ(sidecar.at("capturedAt"), "2026-01-04T04:05:06Z");
  EXPECT_TRUE(sidecar.at("snapshots").empty());

  const auto& lanes = sidecar.at("lanes");
  ASSERT_EQ(lanes.size(), 2u);
  EXPECT_EQ(lanes[0].at("name"), "editor");
  EXPECT_EQ(lanes[0].at("kind"), "frame");
  EXPECT_EQ(lanes[0].at("samples"), 2);
  EXPECT_EQ(lanes[1].at("kind"), "server");
  EXPECT_EQ(lanes[1].at("samples"), 0);
}

TEST(ProfilerCapture, SidecarListsNodesWithIdsAndParents)
{
  Histories histories;
  const auto sidecar = captureSidecar(inputFor(histories));
  const auto& nodes = sidecar.at("lanes")[0].at("nodes");

  ASSERT_EQ(nodes.size(), 5u);
  EXPECT_EQ(nodes[4].at("id"), 4);
  EXPECT_EQ(nodes[4].at("parent"), 3);
  EXPECT_EQ(nodes[4].at("name"), "Present");
  EXPECT_EQ(nodes[0].at("name"), "");
}

TEST(ProfilerCapture, SidecarCarriesSnapshots)
{
  Histories histories;
  auto input = inputFor(histories);
  input.snapshots.push_back({12.5, 2048, "join", 1.25, 3});

  const auto sidecar = captureSidecar(input);
  const auto& snapshots = sidecar.at("snapshots");

  ASSERT_EQ(snapshots.size(), 1u);
  EXPECT_EQ(snapshots[0].at("secondsAgo"), 12.5);
  EXPECT_EQ(snapshots[0].at("bytes"), 2048);
  EXPECT_EQ(snapshots[0].at("reason"), "join");
  EXPECT_EQ(snapshots[0].at("buildMillis"), 1.25);
  EXPECT_EQ(snapshots[0].at("recipients"), 3);
}

TEST(ProfilerCapture, SidecarStringsAreOnlyTheKnownFields)
{
  Histories histories;
  auto input = inputFor(histories);
  input.capturedAt = "2026-01-04T04:05:06Z";
  input.snapshots.push_back({1.0, 10, "join", 1.0, 1});

  std::set<std::string> found;
  collectStrings(captureSidecar(input), found);

  const std::set<std::string> expected{
    "1.2.3", "Release", "Windows", "editor", "Arena", "2026-01-04T04:05:06Z", "frame", "server", "", "Network in",
    "Input", "Renderer", "Present", "Server tick", "Scripts", "Physics", "join"
  };

  EXPECT_EQ(found, expected);
  EXPECT_TRUE(found.contains("Arena"));
}

TEST(ProfilerCapture, BaseNameAndTimestampUseUtcFromTheGivenTime)
{
  EXPECT_EQ(captureBaseName("editor", at(1767499506)), "editor-20260104-040506");
  EXPECT_EQ(captureTimestamp(at(1767499506)), "2026-01-04T04:05:06Z");
  EXPECT_EQ(captureBaseName("editor", at(1709251199)), "editor-20240229-235959");
  EXPECT_EQ(captureBaseName("editor", at(1709251200)), "editor-20240301-000000");
  EXPECT_EQ(captureBaseName("editor", at(0)), "editor-19700101-000000");
}

TEST(ProfilerCapture, DatesBeforeTheEpochAndFarInTheFutureFormatCorrectly)
{
  EXPECT_EQ(captureTimestamp(at(-1)), "1969-12-31T23:59:59Z");
  EXPECT_EQ(captureBaseName("editor", at(-1)), "editor-19691231-235959");
  EXPECT_EQ(captureTimestamp(at(-58017600)), "1968-02-29T12:00:00Z");
  EXPECT_EQ(captureTimestamp(at(4107542399)), "2100-02-28T23:59:59Z");
  EXPECT_EQ(captureTimestamp(at(4107542400)), "2100-03-01T00:00:00Z");
  // libstdc++ counts system_clock in int64 nanoseconds, so its range ends in 2262; stay inside it.
  EXPECT_EQ(captureTimestamp(at(7263215999)), "2200-02-28T23:59:59Z");
  EXPECT_EQ(captureTimestamp(at(7263216000)), "2200-03-01T00:00:00Z");
  EXPECT_EQ(captureTimestamp(at(7389360000)), "2204-02-29T00:00:00Z");
  EXPECT_EQ(captureTimestamp(at(7415884800)), "2205-01-01T00:00:00Z");
  EXPECT_EQ(captureBaseName("editor", at(9214646399)), "editor-22611231-235959");
}

TEST(ProfilerCapture, SidecarRecordsPausedAndServerState)
{
  Histories histories;
  auto input = inputFor(histories);

  EXPECT_FALSE(captureSidecar(input).at("paused").get<bool>());
  EXPECT_FALSE(captureSidecar(input).contains("serverState"));

  input.paused = true;
  input.serverState = captureServerStateName(ServerLaneState::disconnected);
  const auto sidecar = captureSidecar(input);

  EXPECT_TRUE(sidecar.at("paused").get<bool>());
  EXPECT_EQ(sidecar.at("serverState"), "disconnected");
  EXPECT_STREQ(captureServerStateName(ServerLaneState::live), "live");
}

TEST(ProfilerCapture, HasSamplesNeedsAtLeastOneNonEmptyLane)
{
  Histories histories;
  EXPECT_FALSE(captureHasSamples(inputFor(histories)));
  EXPECT_FALSE(captureHasSamples(CaptureInput{}));

  histories.server.push(serverSample(1));
  EXPECT_TRUE(captureHasSamples(inputFor(histories)));
}

TEST_F(ProfilerCaptureFiles, WriteCaptureCreatesTheFolderAndBothFiles)
{
  Histories histories;
  histories.editor.push(editorSample(1));
  histories.server.push(serverSample(2));

  const auto folder = m_directory / "captures";
  ASSERT_FALSE(std::filesystem::exists(folder));

  const auto result = writeCapture(inputFor(histories), folder, at(1767499506));

  ASSERT_TRUE(result.ok()) << result.error;
  EXPECT_EQ(result.csvPath, folder / "editor-20260104-040506.csv");

  std::ifstream csv(result.csvPath, std::ios::binary);
  const std::string csvText((std::istreambuf_iterator<char>(csv)), std::istreambuf_iterator<char>());
  EXPECT_EQ(lines(csvText).size(), 3u);
  EXPECT_EQ(csvText.find('\r'), std::string::npos);

  std::ifstream sidecarFile(folder / "editor-20260104-040506.json");
  const auto sidecar = nlohmann::json::parse(sidecarFile);
  EXPECT_EQ(sidecar.at("capturedAt"), "2026-01-04T04:05:06Z");
  EXPECT_EQ(sidecar.at("sceneName"), "Arena");
}

TEST_F(ProfilerCaptureFiles, WriteCaptureReportsAnUncreatableFolder)
{
  std::filesystem::create_directories(m_directory);
  const auto blocker = m_directory / "file";
  std::ofstream(blocker) << "x";

  Histories histories;
  histories.editor.push(editorSample(1));
  const auto result = writeCapture(inputFor(histories), blocker / "captures", at(0));

  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(result.csvPath.empty());
}

TEST_F(ProfilerCaptureFiles, WriteCaptureWithNothingToSaveWritesNoFiles)
{
  Histories histories;
  const auto folder = m_directory / "captures";

  const auto result = writeCapture(inputFor(histories), folder, at(1767499506));

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error, "Nothing to save yet");
  EXPECT_FALSE(std::filesystem::exists(folder));

  histories.editor.push(editorSample(1));
  EXPECT_TRUE(writeCapture(inputFor(histories), folder, at(1767499506)).ok());
}

TEST_F(ProfilerCaptureFiles, InvalidUtf8InANodeNameStillWritesAParsableSidecar)
{
  Histories histories;
  histories.editor.push(editorSample(1));

  auto input = inputFor(histories);
  input.lanes[0].nodes[2].name = "In\xff" "put";
  input.snapshots.push_back({1.0, 10, "jo\xc3" "in", 1.0, 1});

  const auto folder = m_directory / "captures";
  const auto result = writeCapture(input, folder, at(1767499506));

  ASSERT_TRUE(result.ok()) << result.error;

  std::ifstream sidecarFile(folder / "editor-20260104-040506.json");
  const auto sidecar = nlohmann::json::parse(sidecarFile);
  EXPECT_NE(sidecar.at("lanes")[0].at("nodes")[2].at("name").get<std::string>().find("put"), std::string::npos);
}

TEST_F(ProfilerCaptureFiles, ASidecarWriteFailureRemovesTheCsv)
{
  Histories histories;
  histories.editor.push(editorSample(1));

  const auto folder = m_directory / "captures";
  std::filesystem::create_directories(folder / "editor-20260104-040506.json");
  std::ofstream(folder / "editor-20260104-040506.json" / "keep") << "x";

  const auto result = writeCapture(inputFor(histories), folder, at(1767499506));

  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(result.csvPath.empty());
  EXPECT_FALSE(std::filesystem::exists(folder / "editor-20260104-040506.csv"));
  EXPECT_TRUE(std::filesystem::exists(folder / "editor-20260104-040506.json" / "keep"));
}
