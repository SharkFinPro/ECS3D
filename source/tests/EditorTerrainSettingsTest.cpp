#include <gtest/gtest.h>

#include "EditorTerrainSettings.h"
#include "SettingsStore.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>

namespace {
  class EditorTerrainSettings : public testing::Test {
  protected:
    void SetUp() override
    {
      const auto* info = testing::UnitTest::GetInstance()->current_test_info();

      m_directory = std::filesystem::temp_directory_path() /
                    ("ecs3d-" + std::string(info->test_suite_name()) + "-" + std::string(info->name()));

      std::error_code error;
      std::filesystem::remove_all(m_directory, error);

      m_file = m_directory / "settings.json";
    }

    void TearDown() override
    {
      std::error_code error;
      std::filesystem::remove_all(m_directory, error);
    }

    void writeFile(const std::string& contents) const
    {
      std::filesystem::create_directories(m_directory);
      std::ofstream out(m_file, std::ios::trunc);
      out << contents;
    }

    [[nodiscard]] nlohmann::json readFile() const
    {
      std::ifstream in(m_file);
      nlohmann::json parsed;
      in >> parsed;

      return parsed;
    }

    std::filesystem::path m_directory;
    std::filesystem::path m_file;
  };

  constexpr float notANumber = std::numeric_limits<float>::quiet_NaN();
  constexpr float infinity = std::numeric_limits<float>::infinity();
}

TEST_F(EditorTerrainSettings, ToolIdsMatchTheEditorOrder)
{
  EXPECT_EQ(editorTerrainSettings::toolCount, 5u);
  EXPECT_STREQ(editorTerrainSettings::toolId(0), "add");
  EXPECT_STREQ(editorTerrainSettings::toolId(1), "subtract");
  EXPECT_STREQ(editorTerrainSettings::toolId(2), "smooth");
  EXPECT_STREQ(editorTerrainSettings::toolId(3), "flatten");
  EXPECT_STREQ(editorTerrainSettings::toolId(4), "paint");
  EXPECT_EQ(editorTerrainSettings::toolId(5), nullptr);
}

TEST_F(EditorTerrainSettings, EveryToolReadsTheDefaultsFromAnEmptyStore)
{
  const SettingsStore store(m_file);

  for (std::size_t tool = 0; tool < editorTerrainSettings::toolCount; ++tool)
  {
    const editorTerrainSettings::BrushSettings brush = editorTerrainSettings::readBrush(store, tool);

    EXPECT_FLOAT_EQ(brush.radius, 10.0f) << tool;
    EXPECT_FLOAT_EQ(brush.strength, tool == 1 ? editorTerrainSettings::defaultSubtractStrength : 0.5f) << tool;
    EXPECT_FLOAT_EQ(brush.falloff, 0.5f) << tool;
  }

  EXPECT_EQ(editorTerrainSettings::readLastTool(store), 0u);
}

TEST_F(EditorTerrainSettings, ThePaintToolRoundTripsItsBrushAndBecomesTheLastTool)
{
  {
    SettingsStore store(m_file);
    editorTerrainSettings::writeBrush(store, 4, { 3.0f, 0.2f, 0.6f });
    editorTerrainSettings::writeLastTool(store, 4);
    store.flush();
  }

  const SettingsStore reloaded(m_file);
  const editorTerrainSettings::BrushSettings paint = editorTerrainSettings::readBrush(reloaded, 4);

  EXPECT_FLOAT_EQ(paint.radius, 3.0f);
  EXPECT_FLOAT_EQ(paint.strength, 0.2f);
  EXPECT_FLOAT_EQ(paint.falloff, 0.6f);
  EXPECT_EQ(editorTerrainSettings::readLastTool(reloaded), 4u);
  EXPECT_EQ(readFile().at("editor.terrain.tool").get<std::string>(), "paint");
}

TEST_F(EditorTerrainSettings, AFileWithOnlyTheFourOriginalToolsStillLoadsAndPaintReadsDefaults)
{
  {
    SettingsStore store(m_file);
    editorTerrainSettings::writeBrush(store, 3, { 4.0f, 0.9f, 0.1f });
    editorTerrainSettings::writeLastTool(store, 3);
    store.flush();
  }

  const SettingsStore reloaded(m_file);

  EXPECT_FLOAT_EQ(editorTerrainSettings::readBrush(reloaded, 3).radius, 4.0f);
  EXPECT_EQ(editorTerrainSettings::readLastTool(reloaded), 3u);
  EXPECT_FLOAT_EQ(editorTerrainSettings::readBrush(reloaded, 4).radius, editorTerrainSettings::defaultRadius);
  EXPECT_FLOAT_EQ(editorTerrainSettings::readBrush(reloaded, 4).strength, editorTerrainSettings::defaultStrength);
}

TEST_F(EditorTerrainSettings, BrushesRoundTripPerToolWithoutAffectingTheOthers)
{
  {
    SettingsStore store(m_file);
    editorTerrainSettings::writeBrush(store, 1, { 25.0f, 0.25f, 0.75f });
    editorTerrainSettings::writeBrush(store, 3, { 0.5f, 1.0f, 0.0f });
    store.flush();
  }

  const SettingsStore reloaded(m_file);
  const editorTerrainSettings::BrushSettings subtract = editorTerrainSettings::readBrush(reloaded, 1);
  const editorTerrainSettings::BrushSettings flatten = editorTerrainSettings::readBrush(reloaded, 3);
  const editorTerrainSettings::BrushSettings add = editorTerrainSettings::readBrush(reloaded, 0);
  const editorTerrainSettings::BrushSettings smooth = editorTerrainSettings::readBrush(reloaded, 2);

  EXPECT_FLOAT_EQ(subtract.radius, 25.0f);
  EXPECT_FLOAT_EQ(subtract.strength, 0.25f);
  EXPECT_FLOAT_EQ(subtract.falloff, 0.75f);
  EXPECT_FLOAT_EQ(flatten.radius, 0.5f);
  EXPECT_FLOAT_EQ(flatten.strength, 1.0f);
  EXPECT_FLOAT_EQ(flatten.falloff, 0.0f);
  EXPECT_FLOAT_EQ(add.radius, 10.0f);
  EXPECT_FLOAT_EQ(smooth.strength, 0.5f);
}

TEST_F(EditorTerrainSettings, BrushValuesAreStoredUnderTheDocumentedKeys)
{
  SettingsStore store(m_file);
  editorTerrainSettings::writeBrush(store, 2, { 12.0f, 0.4f, 0.6f });
  store.flush();

  const nlohmann::json written = readFile();

  EXPECT_FLOAT_EQ(written.at("editor.terrain.smooth.radius").get<float>(), 12.0f);
  EXPECT_FLOAT_EQ(written.at("editor.terrain.smooth.strength").get<float>(), 0.4f);
  EXPECT_FLOAT_EQ(written.at("editor.terrain.smooth.falloff").get<float>(), 0.6f);
}

TEST_F(EditorTerrainSettings, ValuesClampOnReadAndWrite)
{
  writeFile(R"({
    "editor.terrain.add.radius": 99999.0,
    "editor.terrain.add.strength": 7.0,
    "editor.terrain.add.falloff": -3.0,
    "editor.terrain.subtract.radius": 0.0001
  })");

  SettingsStore store(m_file);
  const editorTerrainSettings::BrushSettings add = editorTerrainSettings::readBrush(store, 0);

  EXPECT_FLOAT_EQ(add.radius, editorTerrainSettings::maxRadius);
  EXPECT_FLOAT_EQ(add.strength, editorTerrainSettings::maxStrength);
  EXPECT_FLOAT_EQ(add.falloff, editorTerrainSettings::minFalloff);
  EXPECT_FLOAT_EQ(editorTerrainSettings::readBrush(store, 1).radius, editorTerrainSettings::minRadius);

  editorTerrainSettings::writeBrush(store, 2, { -5.0f, 2.0f, -1.0f });
  const editorTerrainSettings::BrushSettings smooth = editorTerrainSettings::readBrush(store, 2);

  EXPECT_FLOAT_EQ(smooth.radius, editorTerrainSettings::minRadius);
  EXPECT_FLOAT_EQ(smooth.strength, 1.0f);
  EXPECT_FLOAT_EQ(smooth.falloff, 0.0f);
  EXPECT_FLOAT_EQ(editorTerrainSettings::readBrush(store, 3).radius, 10.0f);
}

TEST_F(EditorTerrainSettings, InRangeValuesAreNotClamped)
{
  EXPECT_FLOAT_EQ(editorTerrainSettings::clampRadius(0.1f), 0.1f);
  EXPECT_FLOAT_EQ(editorTerrainSettings::clampRadius(1000.0f), 1000.0f);
  EXPECT_FLOAT_EQ(editorTerrainSettings::clampRadius(37.5f), 37.5f);
  EXPECT_FLOAT_EQ(editorTerrainSettings::clampStrength(0.0f), 0.0f);
  EXPECT_FLOAT_EQ(editorTerrainSettings::clampStrength(0.3f), 0.3f);
  EXPECT_FLOAT_EQ(editorTerrainSettings::clampFalloff(1.0f), 1.0f);
}

TEST_F(EditorTerrainSettings, NonFiniteInputsReturnTheDefaults)
{
  for (const float bad : { notANumber, infinity, -infinity })
  {
    EXPECT_FLOAT_EQ(editorTerrainSettings::clampRadius(bad), editorTerrainSettings::defaultRadius);
    EXPECT_FLOAT_EQ(editorTerrainSettings::clampStrength(bad), editorTerrainSettings::defaultStrength);
    EXPECT_FLOAT_EQ(editorTerrainSettings::clampFalloff(bad), editorTerrainSettings::defaultFalloff);
  }

  SettingsStore store(m_file);
  editorTerrainSettings::writeBrush(store, 0, { notANumber, infinity, -infinity });
  const editorTerrainSettings::BrushSettings brush = editorTerrainSettings::readBrush(store, 0);

  EXPECT_FLOAT_EQ(brush.radius, 10.0f);
  EXPECT_FLOAT_EQ(brush.strength, 0.5f);
  EXPECT_FLOAT_EQ(brush.falloff, 0.5f);
}

TEST_F(EditorTerrainSettings, AValueOfTheWrongJsonTypeFallsBackToTheDefault)
{
  writeFile(R"({
    "editor.terrain.add.radius": "big",
    "editor.terrain.add.strength": {"value": 1},
    "editor.terrain.add.falloff": null,
    "editor.terrain.subtract.radius": [1, 2],
    "editor.terrain.subtract.strength": 0.2,
    "editor.terrain.smooth.radius": 20
  })");

  const SettingsStore store(m_file);
  const editorTerrainSettings::BrushSettings add = editorTerrainSettings::readBrush(store, 0);
  const editorTerrainSettings::BrushSettings subtract = editorTerrainSettings::readBrush(store, 1);

  EXPECT_FLOAT_EQ(add.radius, 10.0f);
  EXPECT_FLOAT_EQ(add.strength, 0.5f);
  EXPECT_FLOAT_EQ(add.falloff, 0.5f);
  EXPECT_FLOAT_EQ(subtract.radius, 10.0f);

  // nlohmann reads a JSON boolean as 0 or 1, so a stored bool is a number here and is clamped like one.
  writeFile(R"({"editor.terrain.add.strength": true})");
  EXPECT_FLOAT_EQ(editorTerrainSettings::readBrush(SettingsStore(m_file), 0).strength, 1.0f);

  // Positive control: a well-typed value beside the bad ones, and an integer where a float is expected, are read.
  EXPECT_FLOAT_EQ(subtract.strength, 0.2f);
  EXPECT_FLOAT_EQ(editorTerrainSettings::readBrush(store, 2).radius, 20.0f);
}

TEST_F(EditorTerrainSettings, AnOutOfRangeToolReadsDefaultsAndWritesNothing)
{
  SettingsStore store(m_file);

  editorTerrainSettings::writeBrush(store, 5, { 50.0f, 0.1f, 0.1f });
  editorTerrainSettings::writeBrush(store, static_cast<std::size_t>(-1), { 50.0f, 0.1f, 0.1f });

  EXPECT_FALSE(store.hasPendingWrite());

  const editorTerrainSettings::BrushSettings brush = editorTerrainSettings::readBrush(store, 5);

  EXPECT_FLOAT_EQ(brush.radius, 10.0f);
  EXPECT_FLOAT_EQ(brush.strength, 0.5f);
  EXPECT_FLOAT_EQ(brush.falloff, 0.5f);

  // Positive control: an in-range write does schedule one.
  editorTerrainSettings::writeBrush(store, 0, { 50.0f, 0.1f, 0.1f });
  EXPECT_TRUE(store.hasPendingWrite());
}

TEST_F(EditorTerrainSettings, TheLastToolRoundTripsAsItsId)
{
  {
    SettingsStore store(m_file);
    editorTerrainSettings::writeLastTool(store, 3);
    store.flush();
  }

  EXPECT_EQ(readFile().at("editor.terrain.tool").get<std::string>(), "flatten");

  const SettingsStore reloaded(m_file);
  EXPECT_EQ(editorTerrainSettings::readLastTool(reloaded), 3u);
}

TEST_F(EditorTerrainSettings, AnUnknownOrMistypedLastToolReadsZero)
{
  writeFile(R"({"editor.terrain.tool": "erode"})");
  EXPECT_EQ(editorTerrainSettings::readLastTool(SettingsStore(m_file)), 0u);

  writeFile(R"({"editor.terrain.tool": 2})");
  EXPECT_EQ(editorTerrainSettings::readLastTool(SettingsStore(m_file)), 0u);

  writeFile(R"({"editor.terrain.tool": "smooth"})");
  EXPECT_EQ(editorTerrainSettings::readLastTool(SettingsStore(m_file)), 2u);
}

TEST_F(EditorTerrainSettings, AnOutOfRangeLastToolWritesNothing)
{
  SettingsStore store(m_file);

  editorTerrainSettings::writeLastTool(store, 1);
  editorTerrainSettings::writeLastTool(store, 5);

  EXPECT_EQ(editorTerrainSettings::readLastTool(store), 1u);
}

TEST_F(EditorTerrainSettings, WritingStoresTheClampedValueNotTheRawOne)
{
  SettingsStore store(m_file);
  editorTerrainSettings::writeBrush(store, 2, { 5000.0f, -2.0f, 3.0f });

  EXPECT_FLOAT_EQ(store.get<float>("editor.terrain.smooth.radius", -1.0f), editorTerrainSettings::maxRadius);
  EXPECT_FLOAT_EQ(store.get<float>("editor.terrain.smooth.strength", -1.0f), editorTerrainSettings::minStrength);
  EXPECT_FLOAT_EQ(store.get<float>("editor.terrain.smooth.falloff", -1.0f), editorTerrainSettings::maxFalloff);

  editorTerrainSettings::writeBrush(store, 0, { 0.0f, 0.5f, 0.5f });
  EXPECT_FLOAT_EQ(store.get<float>("editor.terrain.add.radius", -1.0f), editorTerrainSettings::minRadius);

  editorTerrainSettings::writeBrush(store, 1, { notANumber, infinity, -infinity });
  EXPECT_FLOAT_EQ(store.get<float>("editor.terrain.subtract.radius", -1.0f), editorTerrainSettings::defaultRadius);
  EXPECT_FLOAT_EQ(store.get<float>("editor.terrain.subtract.strength", -1.0f),
                  editorTerrainSettings::defaultSubtractStrength);
  EXPECT_FLOAT_EQ(store.get<float>("editor.terrain.subtract.falloff", -1.0f), editorTerrainSettings::defaultFalloff);
}
