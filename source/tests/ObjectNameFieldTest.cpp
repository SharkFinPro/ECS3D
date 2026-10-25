#include <gtest/gtest.h>

#include "EditHistoryFixtures.h"
#include "Replication.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "edits/RecordEdits.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"

#include <ObjectNameField.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
  using namespace editHistoryFixtures;

  // Plays the part of the Inspector: owns the text buffer, feeds ObjectNameField what ImGui would report
  // and collects every rename it asks to send.
  class FieldDriver
  {
  public:
    FieldDriver(const uuids::uuid& objectUUID, std::string currentName)
      : m_currentName(std::move(currentName))
    {
      show(objectUUID);
    }

    // The selection-change step of ObjectInspector::display: flush for the old object, then reseed.
    void show(const uuids::uuid& objectUUID)
    {
      if (!m_field.isShowing(objectUUID))
      {
        collect(m_field.flush(m_buffer));
        apply(m_field.select(objectUUID, m_currentName));
      }
    }

    void type(const std::string& text)
    {
      for (const char c : text)
      {
        m_buffer += c;
        step(true, false, true);
      }
    }

    void backspace()
    {
      m_buffer.pop_back();
      step(true, false, true);
    }

    void leaveField(const bool edited = false)
    {
      step(false, true, edited);
    }

    void idleFrame()
    {
      step(false, false, false);
    }

    void activeFrame()
    {
      step(true, false, false);
    }

    void panelFlush()
    {
      collect(m_field.flush(m_buffer));
    }

    void serverRenames(const std::string& name)
    {
      m_currentName = name;
    }

    [[nodiscard]] const std::string& buffer() const { return m_buffer; }

    [[nodiscard]] const std::vector<ObjectNameField::Rename>& sent() const { return m_sent; }

    ObjectNameField m_field;

  private:
    std::string m_buffer;
    std::string m_currentName;
    std::vector<ObjectNameField::Rename> m_sent;

    void step(const bool active, const bool deactivatedAfterEdit, const bool edited)
    {
      apply(m_field.update(m_currentName, m_buffer, active, deactivatedAfterEdit, edited));
    }

    void apply(const ObjectNameField::Result& result)
    {
      collect(result.rename);
      if (result.reseed)
      {
        m_buffer = *result.reseed;
      }
    }

    void collect(const std::optional<ObjectNameField::Rename>& rename)
    {
      if (rename)
      {
        m_sent.push_back(*rename);
      }
    }
  };

  uuids::uuid sunUUID()
  {
    return anotherUUID();
  }
}

TEST(ObjectNameField, SelectingSeedsTheBufferFromTheObjectsName)
{
  FieldDriver driver(sunUUID(), "Sun");

  EXPECT_EQ(driver.buffer(), "Sun");
  EXPECT_TRUE(driver.sent().empty());
}

TEST(ObjectNameField, TypingSendsNothingUntilTheFieldIsLeft)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.type("ny");
  driver.activeFrame();
  EXPECT_TRUE(driver.sent().empty());
  EXPECT_EQ(driver.buffer(), "Sunny");

  // Positive control: leaving the field is what sends.
  driver.leaveField();
  EXPECT_EQ(driver.sent().size(), 1u);
}

TEST(ObjectNameField, LeavingTheFieldSendsOneRenameWithTheWholeName)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.type("ny");
  driver.leaveField();
  driver.idleFrame();
  driver.idleFrame();

  ASSERT_EQ(driver.sent().size(), 1u);
  EXPECT_EQ(driver.sent().front(), (ObjectNameField::Rename{ sunUUID(), "Sunny" }));
}

TEST(ObjectNameField, AnEditThatEndsOnTheCurrentNameSendsNothing)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.type("n");
  driver.backspace();
  driver.leaveField();
  EXPECT_TRUE(driver.sent().empty());

  // Positive control: the same gesture ending on a different name does send.
  FieldDriver changed(sunUUID(), "Sun");
  changed.type("n");
  changed.leaveField();
  EXPECT_EQ(changed.sent().size(), 1u);
}

TEST(ObjectNameField, EscapeRevertSendsNothing)
{
  // Escape restores the text ImGui saw on activation and ends the edit, which it reports as an edit
  // followed by a deactivation.
  FieldDriver reverted(sunUUID(), "Sun");
  reverted.type("ny");
  reverted.backspace();
  reverted.backspace();
  reverted.leaveField(true);
  EXPECT_TRUE(reverted.sent().empty());
  EXPECT_FALSE(reverted.m_field.hasPendingName());

  // Positive control: without the revert the same typing sends.
  FieldDriver kept(sunUUID(), "Sun");
  kept.type("ny");
  kept.leaveField(true);
  EXPECT_EQ(kept.sent().size(), 1u);
}

TEST(ObjectNameField, AnEmptyNameIsSentLikeAnyOther)
{
  FieldDriver driver(sunUUID(), "Sun");

  for (int i = 0; i < 3; ++i)
  {
    driver.backspace();
  }
  driver.leaveField();

  ASSERT_EQ(driver.sent().size(), 1u);
  EXPECT_EQ(driver.sent().front().name, "");
}

TEST(ObjectNameField, SelectingAnotherObjectFlushesTheNameForTheObjectItWasTypedFor)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.type("ny");
  driver.show(someOtherUUID());

  ASSERT_EQ(driver.sent().size(), 1u);
  EXPECT_EQ(driver.sent().front(), (ObjectNameField::Rename{ sunUUID(), "Sunny" }));
  EXPECT_NE(driver.sent().front().objectUUID, someOtherUUID());
}

TEST(ObjectNameField, SelectingAnotherObjectWithNothingTypedSendsNothing)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.activeFrame();
  driver.show(someOtherUUID());
  EXPECT_TRUE(driver.sent().empty());

  // Positive control: the same switch after typing does send.
  FieldDriver typed(sunUUID(), "Sun");
  typed.type("x");
  typed.show(someOtherUUID());
  EXPECT_EQ(typed.sent().size(), 1u);
}

TEST(ObjectNameField, TheNewObjectsBufferIsNotTheOldOnesText)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.type("ny");
  driver.show(someOtherUUID());

  EXPECT_EQ(driver.buffer(), "Sun");
  driver.leaveField();
  EXPECT_EQ(driver.sent().size(), 1u);
}

TEST(ObjectNameField, ADeactivationAndASelectionChangeOnOneFrameSendOnce)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.type("ny");
  driver.leaveField();
  driver.show(someOtherUUID());
  driver.leaveField();
  EXPECT_EQ(driver.sent().size(), 1u);

  // Positive control: the selection change alone, with no deactivation, sends the one rename.
  FieldDriver flushedFirst(sunUUID(), "Sun");
  flushedFirst.type("ny");
  flushedFirst.show(someOtherUUID());
  flushedFirst.leaveField();
  EXPECT_EQ(flushedFirst.sent().size(), 1u);
}

TEST(ObjectNameField, AFlushAfterTheCommitSendsNothingMore)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.type("ny");
  driver.leaveField();
  driver.panelFlush();
  driver.panelFlush();

  EXPECT_EQ(driver.sent().size(), 1u);
}

TEST(ObjectNameField, ThePanelFlushSendsAPendingNameWhenTheObjectIsNoLongerShown)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.type("ny");
  EXPECT_TRUE(driver.m_field.hasPendingName());
  driver.panelFlush();

  ASSERT_EQ(driver.sent().size(), 1u);
  EXPECT_EQ(driver.sent().front(), (ObjectNameField::Rename{ sunUUID(), "Sunny" }));
  EXPECT_FALSE(driver.m_field.hasPendingName());
}

TEST(ObjectNameField, AFieldThatWentIdleWithoutItsDeactivationStillSends)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.type("ny");
  driver.idleFrame();

  EXPECT_EQ(driver.sent().size(), 1u);
}

TEST(ObjectNameField, AnIdleFieldFollowsTheObjectsName)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.serverRenames("Star");
  driver.idleFrame();

  EXPECT_EQ(driver.buffer(), "Star");
  EXPECT_TRUE(driver.sent().empty());
}

TEST(ObjectNameField, AnActiveFieldIsNeverOverwritten)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.type("ny");
  driver.serverRenames("Star");
  driver.activeFrame();
  EXPECT_EQ(driver.buffer(), "Sunny");

  // Positive control: the same name change reaches an idle field.
  FieldDriver idle(sunUUID(), "Sun");
  idle.serverRenames("Star");
  idle.idleFrame();
  EXPECT_EQ(idle.buffer(), "Star");
}

TEST(ObjectNameField, TheTypedNameStaysVisibleUntilTheServerAnswers)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.type("ny");
  driver.leaveField();
  driver.idleFrame();
  driver.idleFrame();
  EXPECT_EQ(driver.buffer(), "Sunny");

  driver.serverRenames("Sunny");
  driver.idleFrame();
  EXPECT_EQ(driver.buffer(), "Sunny");

  // Positive control: once answered, a later outside rename shows through.
  driver.serverRenames("Star");
  driver.idleFrame();
  EXPECT_EQ(driver.buffer(), "Star");
  EXPECT_EQ(driver.sent().size(), 1u);
}

TEST(ObjectNameField, ADifferentNameArrivingWhileTheRenameIsInFlightShowsThrough)
{
  FieldDriver driver(sunUUID(), "Sun");

  driver.type("ny");
  driver.leaveField();
  driver.serverRenames("Moon");
  driver.idleFrame();

  EXPECT_EQ(driver.buffer(), "Moon");
}

TEST(ObjectNameField, AnUntouchedFieldNeverSends)
{
  // A read-only inspector disables the field, so ImGui reports neither an edit nor a deactivation. The
  // inspector also drops any rename while not editable.
  FieldDriver driver(sunUUID(), "Sun");

  for (int i = 0; i < 5; ++i)
  {
    driver.idleFrame();
  }
  driver.panelFlush();
  EXPECT_TRUE(driver.sent().empty());

  // Positive control: the same driver does send once an edit is reported.
  driver.type("x");
  driver.leaveField();
  EXPECT_EQ(driver.sent().size(), 1u);
}

// Each rename the field sends is recorded and applied the way EditorApp does it (the command is derived
// from the pre-edit view, then the edit lands), so the history holds one step per typed rename.
TEST(ObjectNameField, TypingARenameLeavesOneUndoStepThatRestoresTheOldName)
{
  auto scene = makeScene();
  scene.object->setName("Sun");

  FieldDriver driver(scene.object->getUUID(), "Sun");
  driver.type("ny");
  driver.leaveField();

  edits::EditHistory history;
  for (const auto& rename : driver.sent())
  {
    const auto edit = replication::buildRenameObject(rename.objectUUID, rename.name);
    const auto command = edits::commandForSceneEdit(edit, *scene.objectManager);
    ASSERT_TRUE(command.has_value());
    history.record(*command);
    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, edit), replication::SceneEditResult::applied);
  }

  EXPECT_EQ(history.undoDepth(), 1u);
  EXPECT_EQ(scene.object->getName(), "Sunny");
  EXPECT_EQ(history.nextUndoLabel(*scene.objectManager), "Rename Sunny");

  const auto outcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(outcome.ok());
  ASSERT_TRUE(outcome.jsonPayload.has_value());
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, *outcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(scene.object->getName(), "Sun");
}

TEST(ObjectNameField, ARenameOfAnObjectThatIsGoneIsRefusedAndNotRecorded)
{
  auto scene = makeScene();
  const auto edit = replication::buildRenameObject(someOtherUUID(), "Ghost");

  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, edit), replication::SceneEditResult::unknownObject);
  EXPECT_FALSE(edits::commandForSceneEdit(edit, *scene.objectManager).has_value());

  // Positive control: the same edit against a live object applies and records.
  const auto live = replication::buildRenameObject(scene.object->getUUID(), "Ghost");
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, live), replication::SceneEditResult::applied);
  EXPECT_TRUE(edits::commandForSceneEdit(live, *scene.objectManager).has_value());
}
