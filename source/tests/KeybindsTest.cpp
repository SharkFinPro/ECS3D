#include <gtest/gtest.h>

#include "Keybinds.h"
#include "SettingsStore.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace {
  // Same temp-directory-per-test approach as SettingsStoreTest: unique per test, cleaned up after.
  class Keybinds : public testing::Test {
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
}

TEST_F(Keybinds, ParsesAndFormatsRoundTripForVariousChords)
{
  for (const char* text : { "F10", "Ctrl+S", "Ctrl+Shift+Z", "Delete", "A", "0", "F1", "F12", "Space" })
  {
    const auto chord = parseChord(text);
    ASSERT_TRUE(chord.has_value()) << text;
    EXPECT_EQ(formatChord(*chord), text) << text;
  }
}

TEST_F(Keybinds, ParsingIsCaseInsensitiveButFormattingIsCanonical)
{
  const auto lower = parseChord("ctrl+shift+z");
  const auto mixed = parseChord("cTrL+ShIfT+z");

  ASSERT_TRUE(lower.has_value());
  ASSERT_TRUE(mixed.has_value());

  EXPECT_EQ(*lower, *mixed);
  EXPECT_EQ(formatChord(*lower), "Ctrl+Shift+Z");
}

TEST_F(Keybinds, ModifierOrderInTheInputDoesNotAffectTheParsedChord)
{
  // Shift+Ctrl+S and Ctrl+Shift+S mean the same chord; the canonical spelling always sorts them
  // Ctrl+Shift+Alt+Super.
  const auto reordered = parseChord("Shift+Ctrl+S");
  ASSERT_TRUE(reordered.has_value());

  EXPECT_EQ(formatChord(*reordered), "Ctrl+Shift+S");
}

TEST_F(Keybinds, RejectsAModifierWithNoKey)
{
  EXPECT_FALSE(parseChord("Ctrl").has_value());
  EXPECT_FALSE(parseChord("Ctrl+").has_value());
  EXPECT_FALSE(parseChord("Ctrl+Shift").has_value());
}

TEST_F(Keybinds, RejectsGarbageAndEmptyStrings)
{
  EXPECT_FALSE(parseChord("").has_value());
  EXPECT_FALSE(parseChord("NotAKey").has_value());
  EXPECT_FALSE(parseChord("Ctrl+NotAKey").has_value());
  EXPECT_FALSE(parseChord("Xyz+S").has_value());
}

TEST_F(Keybinds, DefaultTableMatchesTheSpecCatalogue)
{
  const KeybindTable table;

  EXPECT_EQ(formatChord(*table.binding(EditorAction::toggleGui)), "F10");
  EXPECT_EQ(formatChord(*table.binding(EditorAction::saveProject)), "Ctrl+S");
  EXPECT_EQ(formatChord(*table.binding(EditorAction::saveProjectAs)), "Ctrl+Shift+S");
  EXPECT_EQ(formatChord(*table.binding(EditorAction::undo)), "Ctrl+Z");
  EXPECT_EQ(formatChord(*table.binding(EditorAction::redo)), "Ctrl+Shift+Z");
  EXPECT_EQ(formatChord(*table.binding(EditorAction::deleteSelection)), "Delete");
  EXPECT_EQ(formatChord(*table.binding(EditorAction::duplicateSelection)), "Ctrl+D");
  EXPECT_EQ(formatChord(*table.binding(EditorAction::focusSelection)), "F");

  // Behavior arrives later, but the gizmo actions are already bindable, per spec - unbound by default.
  EXPECT_FALSE(table.binding(EditorAction::gizmoTranslate).has_value());
  EXPECT_FALSE(table.binding(EditorAction::gizmoRotate).has_value());
  EXPECT_FALSE(table.binding(EditorAction::gizmoScale).has_value());
}

TEST_F(Keybinds, AssignRefusesAChordAlreadyHeldByAnotherAction)
{
  SettingsStore settings(m_file);
  KeybindTable table;
  table.load(settings);

  const auto saveChord = *table.binding(EditorAction::saveProject);

  const auto outcome = table.assign(EditorAction::focusSelection, saveChord, settings);

  EXPECT_EQ(outcome.result, KeybindTable::AssignResult::refused);
  ASSERT_TRUE(outcome.heldBy.has_value());
  EXPECT_EQ(*outcome.heldBy, EditorAction::saveProject);

  // Neither action's binding moved: the refusal must not have half-applied.
  EXPECT_EQ(table.binding(EditorAction::focusSelection), parseChord("F"));
  EXPECT_EQ(table.binding(EditorAction::saveProject), saveChord);
}

TEST_F(Keybinds, PositiveControlAssigningAFreeChordSucceeds)
{
  // The control for the refusal test above: a chord nothing holds does assign, proving assign() isn't
  // just always refusing.
  SettingsStore settings(m_file);
  KeybindTable table;
  table.load(settings);

  const auto freeChord = *parseChord("Ctrl+Alt+K");

  const auto outcome = table.assign(EditorAction::focusSelection, freeChord, settings);

  EXPECT_EQ(outcome.result, KeybindTable::AssignResult::assigned);
  EXPECT_FALSE(outcome.heldBy.has_value());
  EXPECT_EQ(table.binding(EditorAction::focusSelection), freeChord);
}

TEST_F(Keybinds, UnbindingFreesAChordForAnotherAction)
{
  SettingsStore settings(m_file);
  KeybindTable table;
  table.load(settings);

  const auto saveChord = *table.binding(EditorAction::saveProject);

  // Before freeing it, the chord is still held - the same refusal machinery as above.
  ASSERT_EQ(table.assign(EditorAction::focusSelection, saveChord, settings).result,
           KeybindTable::AssignResult::refused);

  table.unbind(EditorAction::saveProject, settings);
  EXPECT_FALSE(table.binding(EditorAction::saveProject).has_value());

  const auto outcome = table.assign(EditorAction::focusSelection, saveChord, settings);
  EXPECT_EQ(outcome.result, KeybindTable::AssignResult::assigned);
  EXPECT_EQ(table.binding(EditorAction::focusSelection), saveChord);
}

TEST_F(Keybinds, ResetClearsTheStoredKeyAndReturnsToTheDefault)
{
  SettingsStore settings(m_file);
  KeybindTable table;
  table.load(settings);

  table.assign(EditorAction::focusSelection, *parseChord("Ctrl+Alt+K"), settings);
  settings.flush();
  ASSERT_TRUE(readFile().contains("keybinds.focusSelection"));

  table.reset(EditorAction::focusSelection, settings);
  settings.flush();

  EXPECT_FALSE(readFile().contains("keybinds.focusSelection"));
  EXPECT_EQ(table.binding(EditorAction::focusSelection), parseChord("F"));
}

TEST_F(Keybinds, ResetAllClearsEveryStoredKeyAndRestoresAllDefaults)
{
  SettingsStore settings(m_file);
  KeybindTable table;
  table.load(settings);

  table.assign(EditorAction::focusSelection, *parseChord("Ctrl+Alt+K"), settings);
  table.unbind(EditorAction::toggleGui, settings);
  settings.flush();

  table.resetAll(settings);
  settings.flush();

  const auto written = readFile();
  for (const auto& info : editorActions())
  {
    EXPECT_FALSE(written.contains(std::string("keybinds.") + info.id)) << info.id;
    EXPECT_EQ(table.binding(info.action), info.defaultChord) << info.id;
  }
}

TEST_F(Keybinds, ABindingSurvivesAFreshTableOverTheSameFile)
{
  {
    SettingsStore settings(m_file);
    KeybindTable table;
    table.load(settings);

    table.assign(EditorAction::focusSelection, *parseChord("Ctrl+Alt+K"), settings);
    table.unbind(EditorAction::toggleGui, settings);
    settings.flush();
  }

  const SettingsStore reloadedSettings(m_file);
  KeybindTable reloadedTable;
  reloadedTable.load(reloadedSettings);

  EXPECT_EQ(reloadedTable.binding(EditorAction::focusSelection), parseChord("Ctrl+Alt+K"));
  EXPECT_FALSE(reloadedTable.binding(EditorAction::toggleGui).has_value());

  // Untouched actions still follow their defaults.
  EXPECT_EQ(reloadedTable.binding(EditorAction::saveProject), parseChord("Ctrl+S"));
}

TEST_F(Keybinds, AHandEditedDuplicateResolvesToAValidBijectionOnLoad)
{
  // Both actions written to the same chord by hand: the store-sourced values tie, so declaration order
  // (toggleGui before saveProject) decides, and the loser is left unbound rather than the table holding
  // two actions on one chord.
  writeFile(R"({"keybinds.toggleGui": "Ctrl+S", "keybinds.saveProject": "Ctrl+S"})");

  const SettingsStore settings(m_file);
  KeybindTable table;
  table.load(settings);

  EXPECT_EQ(table.binding(EditorAction::toggleGui), parseChord("Ctrl+S"));
  EXPECT_FALSE(table.binding(EditorAction::saveProject).has_value());

  // The table stays a bijection: no two actions share a chord.
  for (const auto& a : editorActions())
  {
    for (const auto& b : editorActions())
    {
      if (a.action == b.action)
      {
        continue;
      }

      const auto chordA = table.binding(a.action);
      const auto chordB = table.binding(b.action);
      if (chordA.has_value() && chordB.has_value())
      {
        EXPECT_NE(*chordA, *chordB) << a.id << " vs " << b.id;
      }
    }
  }
}

TEST_F(Keybinds, AHandEditedGarbageValueFallsBackToTheDefault)
{
  writeFile(R"({"keybinds.saveProject": "not a real chord"})");

  const SettingsStore settings(m_file);
  KeybindTable table;
  table.load(settings);

  EXPECT_EQ(table.binding(EditorAction::saveProject), parseChord("Ctrl+S"));
}
