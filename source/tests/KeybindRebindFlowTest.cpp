#include <gtest/gtest.h>

#include <KeyCapture.h>
#include <KeybindRebindFlow.h>
#include <Keybinds.h>
#include <SettingsStore.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace {
  // A settings file per test in the temp directory, so the real per-user one is never touched.
  class KeybindRebindFlowTest : public testing::Test {
  protected:
    void SetUp() override
    {
      const auto* info = testing::UnitTest::GetInstance()->current_test_info();

      m_directory = std::filesystem::temp_directory_path() /
                    ("ecs3d-" + std::string(info->test_suite_name()) + "-" + std::string(info->name()));

      std::error_code error;
      std::filesystem::remove_all(m_directory, error);

      m_settings = std::make_unique<SettingsStore>(m_directory / "settings.json");
      m_table.load(*m_settings);
      m_flow = std::make_unique<KeybindRebindFlow>(m_table, *m_settings);
    }

    void TearDown() override
    {
      m_flow.reset();
      m_settings.reset();

      std::error_code error;
      std::filesystem::remove_all(m_directory, error);
    }

    [[nodiscard]] KeyChord defaultChord(const EditorAction action) const
    {
      return *actionInfo(action).defaultChord;
    }

    // What SettingsPanel does: arm the dispatcher's capture when the flow begins one, then reconcile after
    // each action, as displayKeybinds does at the end of a frame.
    void rebind(const EditorAction action)
    {
      m_flow->beginCapture(action);
      m_capture.begin([this, action](const KeyChord& chord) { m_flow->captureChord(action, chord); });
    }

    void sync(const bool visible = true)
    {
      if (m_flow->reconcileCapture(m_capture.active(), visible))
      {
        m_capture.cancel();
      }
    }

    // A non-modifier key press while capturing, as the dispatcher routes it.
    void pressKey(const KeyChord& chord)
    {
      if (m_capture.active())
      {
        m_capture.complete(chord);
      }
    }

    std::filesystem::path m_directory;
    std::unique_ptr<SettingsStore> m_settings;
    KeybindTable m_table;
    std::unique_ptr<KeybindRebindFlow> m_flow;
    KeyCapture m_capture;
  };
}

TEST_F(KeybindRebindFlowTest, CapturingAFreeChordAssignsItAndClearsTheCapture)
{
  const KeyChord freeChord = *parseChord("Ctrl+Alt+F9");
  ASSERT_FALSE(m_table.actionFor(freeChord).has_value());

  m_flow->beginCapture(EditorAction::focusSelection);
  EXPECT_EQ(m_flow->capturingAction(), EditorAction::focusSelection);

  m_flow->captureChord(EditorAction::focusSelection, freeChord);

  EXPECT_EQ(m_table.binding(EditorAction::focusSelection), freeChord);
  EXPECT_FALSE(m_flow->capturingAction().has_value());
  EXPECT_FALSE(m_flow->conflict().has_value());
}

TEST_F(KeybindRebindFlowTest, CapturingAHeldChordRefusesAndNamesTheHolder)
{
  const KeyChord saveChord = defaultChord(EditorAction::saveProject);
  const auto previous = m_table.binding(EditorAction::focusSelection);

  m_flow->beginCapture(EditorAction::focusSelection);
  m_flow->captureChord(EditorAction::focusSelection, saveChord);

  EXPECT_EQ(m_table.binding(EditorAction::focusSelection), previous);
  EXPECT_EQ(m_table.binding(EditorAction::saveProject), saveChord);
  EXPECT_FALSE(m_flow->capturingAction().has_value());
  ASSERT_TRUE(m_flow->conflict().has_value());
  EXPECT_EQ(m_flow->conflict()->action, EditorAction::focusSelection);
  EXPECT_EQ(m_flow->conflict()->heldBy, EditorAction::saveProject);
  EXPECT_EQ(m_flow->conflict()->requested, saveChord);
  EXPECT_TRUE(m_flow->conflict()->offerChooseAnother);
}

TEST_F(KeybindRebindFlowTest, ChooseAnotherKeyRestartsCaptureForTheSameAction)
{
  m_flow->captureChord(EditorAction::focusSelection, defaultChord(EditorAction::saveProject));
  ASSERT_TRUE(m_flow->conflict().has_value());

  const auto restarted = m_flow->chooseAnotherKey();

  EXPECT_EQ(restarted, EditorAction::focusSelection);
  EXPECT_EQ(m_flow->capturingAction(), EditorAction::focusSelection);
  EXPECT_FALSE(m_flow->conflict().has_value());
  EXPECT_FALSE(m_flow->chooseAnotherKey().has_value());
}

TEST_F(KeybindRebindFlowTest, GoToBindingYieldsTheHolderOnceAndClearsTheConflict)
{
  EXPECT_FALSE(m_flow->takeScrollTarget().has_value());

  m_flow->captureChord(EditorAction::focusSelection, defaultChord(EditorAction::saveProject));
  m_flow->goToBinding();

  EXPECT_FALSE(m_flow->conflict().has_value());
  EXPECT_EQ(m_flow->takeScrollTarget(), EditorAction::saveProject);
  EXPECT_FALSE(m_flow->takeScrollTarget().has_value());
}

TEST_F(KeybindRebindFlowTest, CloseDismissesTheConflictWithoutScrolling)
{
  m_flow->captureChord(EditorAction::focusSelection, defaultChord(EditorAction::saveProject));

  m_flow->closeConflict();

  EXPECT_FALSE(m_flow->conflict().has_value());
  EXPECT_FALSE(m_flow->takeScrollTarget().has_value());
}

TEST_F(KeybindRebindFlowTest, RefusedResetConflictCarriesTheDefaultChordAndOffersNoRetry)
{
  const KeyChord saveChord = defaultChord(EditorAction::saveProject);
  m_flow->unbind(EditorAction::saveProject);
  m_flow->captureChord(EditorAction::focusSelection, saveChord);
  ASSERT_EQ(m_table.binding(EditorAction::focusSelection), saveChord);
  ASSERT_FALSE(m_flow->conflict().has_value());

  m_flow->reset(EditorAction::saveProject);

  EXPECT_FALSE(m_table.binding(EditorAction::saveProject).has_value());
  ASSERT_TRUE(m_flow->conflict().has_value());
  EXPECT_EQ(m_flow->conflict()->action, EditorAction::saveProject);
  EXPECT_EQ(m_flow->conflict()->heldBy, EditorAction::focusSelection);
  EXPECT_EQ(m_flow->conflict()->requested, saveChord);
  EXPECT_FALSE(m_flow->conflict()->offerChooseAnother);
}

TEST_F(KeybindRebindFlowTest, ASuccessfulResetRestoresTheDefaultWithoutAConflict)
{
  m_flow->unbind(EditorAction::saveProject);

  m_flow->reset(EditorAction::saveProject);

  EXPECT_EQ(m_table.binding(EditorAction::saveProject), defaultChord(EditorAction::saveProject));
  EXPECT_FALSE(m_flow->conflict().has_value());
}

TEST_F(KeybindRebindFlowTest, UnbindResetAndResetAllEachEndAnInProgressCapture)
{
  m_flow->beginCapture(EditorAction::saveProject);
  m_flow->unbind(EditorAction::saveProject);
  EXPECT_FALSE(m_flow->capturingAction().has_value());
  EXPECT_FALSE(m_table.binding(EditorAction::saveProject).has_value());

  m_flow->beginCapture(EditorAction::saveProject);
  m_flow->reset(EditorAction::saveProject);
  EXPECT_FALSE(m_flow->capturingAction().has_value());
  EXPECT_EQ(m_table.binding(EditorAction::saveProject), defaultChord(EditorAction::saveProject));

  m_flow->beginCapture(EditorAction::undo);
  m_flow->resetAll();
  EXPECT_FALSE(m_flow->capturingAction().has_value());
}

TEST_F(KeybindRebindFlowTest, CancelEndsTheCaptureWithoutAssigning)
{
  const auto before = m_table.binding(EditorAction::saveProject);

  m_flow->beginCapture(EditorAction::saveProject);
  ASSERT_TRUE(m_flow->capturingAction().has_value());
  m_flow->cancelCapture();

  EXPECT_FALSE(m_flow->capturingAction().has_value());
  EXPECT_FALSE(m_flow->conflict().has_value());
  EXPECT_EQ(m_table.binding(EditorAction::saveProject), before);
}

TEST_F(KeybindRebindFlowTest, ACapturePressAssignsTheKeyWhenNothingCancelledIt)
{
  const KeyChord freeChord = *parseChord("Ctrl+Alt+F9");

  rebind(EditorAction::focusSelection);
  sync();
  pressKey(freeChord);

  EXPECT_EQ(m_table.binding(EditorAction::focusSelection), freeChord);
}

TEST_F(KeybindRebindFlowTest, CancelStopsTheDispatcherCaptureSoTheNextKeyAssignsNothing)
{
  const KeyChord freeChord = *parseChord("Ctrl+Alt+F9");
  const auto before = m_table.binding(EditorAction::focusSelection);

  rebind(EditorAction::focusSelection);
  m_flow->cancelCapture();
  sync();
  pressKey(freeChord);

  EXPECT_FALSE(m_capture.active());
  EXPECT_EQ(m_table.binding(EditorAction::focusSelection), before);
  EXPECT_FALSE(m_table.actionFor(freeChord).has_value());
}

TEST_F(KeybindRebindFlowTest, UnbindResetAndResetAllStopTheDispatcherCapture)
{
  const KeyChord freeChord = *parseChord("Ctrl+Alt+F9");

  rebind(EditorAction::focusSelection);
  m_flow->unbind(EditorAction::saveProject);
  sync();
  pressKey(freeChord);
  EXPECT_FALSE(m_table.actionFor(freeChord).has_value());

  rebind(EditorAction::focusSelection);
  m_flow->reset(EditorAction::saveProject);
  sync();
  pressKey(freeChord);
  EXPECT_FALSE(m_table.actionFor(freeChord).has_value());

  rebind(EditorAction::focusSelection);
  m_flow->resetAll();
  sync();
  pressKey(freeChord);
  EXPECT_FALSE(m_table.actionFor(freeChord).has_value());
}

TEST_F(KeybindRebindFlowTest, HidingTheKeybindsListAbandonsTheCaptureOnBothSides)
{
  const KeyChord freeChord = *parseChord("Ctrl+Alt+F9");

  rebind(EditorAction::focusSelection);
  sync(false);
  pressKey(freeChord);

  EXPECT_FALSE(m_flow->capturingAction().has_value());
  EXPECT_FALSE(m_capture.active());
  EXPECT_FALSE(m_table.actionFor(freeChord).has_value());
}

TEST_F(KeybindRebindFlowTest, TheDispatcherEndingItsCaptureStopsTheFlowShowingThePrompt)
{
  rebind(EditorAction::focusSelection);
  sync();
  ASSERT_EQ(m_flow->capturingAction(), EditorAction::focusSelection);

  m_capture.cancel();
  sync();

  EXPECT_FALSE(m_flow->capturingAction().has_value());
}

TEST_F(KeybindRebindFlowTest, ChooseAnotherKeyKeepsTheRearmedCaptureThroughReconcile)
{
  m_flow->captureChord(EditorAction::focusSelection, defaultChord(EditorAction::saveProject));
  const auto action = m_flow->chooseAnotherKey();
  ASSERT_TRUE(action.has_value());
  m_capture.begin([this, a = *action](const KeyChord& chord) { m_flow->captureChord(a, chord); });
  sync();

  EXPECT_TRUE(m_capture.active());
  EXPECT_EQ(m_flow->capturingAction(), EditorAction::focusSelection);
}
