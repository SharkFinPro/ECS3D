#include <gtest/gtest.h>

#include <PickClick.h>

#include <optional>
#include <string>
#include <uuid.h>

namespace {
  uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto idA = uuidFrom("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
  const auto idB = uuidFrom("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");

  PickClick freshCtrlClick()
  {
    PickClick click;
    click.pressed = true;
    click.wasPressed = false;
    click.ctrl = true;
    click.sceneHovered = true;
    click.gizmoCaptured = false;
    return click;
  }

  bool always(const uuids::uuid&)
  {
    return true;
  }

  bool never(const uuids::uuid&)
  {
    return false;
  }
}

TEST(PickClickRequestTest, AFreshCtrlClickOverTheSceneRequestsAPick)
{
  EXPECT_TRUE(shouldRequestPick(freshCtrlClick()));
}

TEST(PickClickRequestTest, AHeldButtonDoesNotRequestAgain)
{
  auto click = freshCtrlClick();
  click.wasPressed = true;

  EXPECT_FALSE(shouldRequestPick(click));
  EXPECT_TRUE(shouldRequestPick(freshCtrlClick()));
}

TEST(PickClickRequestTest, AReleasedButtonDoesNotRequest)
{
  auto click = freshCtrlClick();
  click.pressed = false;

  EXPECT_FALSE(shouldRequestPick(click));
  EXPECT_TRUE(shouldRequestPick(freshCtrlClick()));
}

TEST(PickClickRequestTest, CtrlIsRequired)
{
  auto click = freshCtrlClick();
  click.ctrl = false;

  EXPECT_FALSE(shouldRequestPick(click));
  EXPECT_TRUE(shouldRequestPick(freshCtrlClick()));
}

TEST(PickClickRequestTest, ACursorOffTheSceneDoesNotRequest)
{
  auto click = freshCtrlClick();
  click.sceneHovered = false;

  EXPECT_FALSE(shouldRequestPick(click));
  EXPECT_TRUE(shouldRequestPick(freshCtrlClick()));
}

TEST(PickClickRequestTest, AGizmoHandleCapturesTheClick)
{
  auto click = freshCtrlClick();
  click.gizmoCaptured = true;

  EXPECT_FALSE(shouldRequestPick(click));
  EXPECT_TRUE(shouldRequestPick(freshCtrlClick()));
}

TEST(PickClickRequestTest, ADefaultClickRequestsNothing)
{
  EXPECT_FALSE(shouldRequestPick(PickClick{}));
}

TEST(PickClickApplyTest, APlainPickReplacesTheSelection)
{
  EditorSelection selection;
  selection.selectObject(idA);

  applyPickResult(selection, idB, false, always);

  EXPECT_EQ(selection.size(), 1u);
  EXPECT_EQ(selection.objectUUID(), std::optional(idB));
  EXPECT_FALSE(selection.contains(idA));
}

TEST(PickClickApplyTest, AShiftPickAddsAnUnselectedObject)
{
  EditorSelection selection;
  selection.selectObject(idA);

  applyPickResult(selection, idB, true, always);

  EXPECT_EQ(selection.size(), 2u);
  EXPECT_TRUE(selection.contains(idA));
  EXPECT_EQ(selection.objectUUID(), std::optional(idB));
}

TEST(PickClickApplyTest, AShiftPickRemovesASelectedObject)
{
  EditorSelection selection;
  selection.selectObject(idA);
  selection.addObject(idB);

  applyPickResult(selection, idB, true, always);

  EXPECT_EQ(selection.size(), 1u);
  EXPECT_TRUE(selection.contains(idA));
  EXPECT_FALSE(selection.contains(idB));
}

TEST(PickClickApplyTest, EmptySpaceClearsTheSelectionWithoutShift)
{
  EditorSelection selection;
  selection.selectObject(idA);

  applyPickResult(selection, std::nullopt, false, always);

  EXPECT_TRUE(selection.empty());
}

TEST(PickClickApplyTest, EmptySpaceWithShiftKeepsTheSelection)
{
  EditorSelection selection;
  selection.selectObject(idA);

  applyPickResult(selection, std::nullopt, true, always);

  EXPECT_EQ(selection.size(), 1u);
  EXPECT_TRUE(selection.contains(idA));

  applyPickResult(selection, std::nullopt, false, always);

  EXPECT_TRUE(selection.empty());
}

TEST(PickClickApplyTest, APickedObjectThatLeftTheSceneChangesNothing)
{
  EditorSelection selection;
  selection.selectObject(idA);

  applyPickResult(selection, idB, false, never);
  applyPickResult(selection, idB, true, never);

  EXPECT_EQ(selection.size(), 1u);
  EXPECT_EQ(selection.objectUUID(), std::optional(idA));

  applyPickResult(selection, idB, false, always);

  EXPECT_EQ(selection.objectUUID(), std::optional(idB));
}

TEST(PickClickApplyTest, TheExistenceCheckReceivesThePickedUuid)
{
  EditorSelection selection;
  selection.selectObject(idA);

  applyPickResult(selection, idB, false, [](const uuids::uuid& uuid) { return uuid == idB; });

  EXPECT_EQ(selection.objectUUID(), std::optional(idB));

  applyPickResult(selection, idA, false, [](const uuids::uuid& uuid) { return uuid == idB; });

  EXPECT_EQ(selection.objectUUID(), std::optional(idB));
}
