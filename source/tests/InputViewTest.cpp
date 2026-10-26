#include <gtest/gtest.h>

#include "InputView.h"

#include <optional>
#include <uuid.h>

TEST(InputView, TheCursorIsMeasuredFromTheViewCornerAndTheRectSizeIsCopied)
{
  const auto camera = uuids::uuid::from_string("0b0d0f6c-3f1c-4f3b-9d56-0d6f4b6a1c11");
  ASSERT_TRUE(camera.has_value());

  input::InputSnapshot snapshot;
  input::setView(snapshot, 350.0f, 260.0f, { 300.0f, 200.0f, 640.0f, 360.0f }, camera);

  EXPECT_EQ(snapshot.view.camera, "0b0d0f6c-3f1c-4f3b-9d56-0d6f4b6a1c11");
  EXPECT_FLOAT_EQ(snapshot.view.cursorX, 50.0f);
  EXPECT_FLOAT_EQ(snapshot.view.cursorY, 60.0f);
  EXPECT_FLOAT_EQ(snapshot.view.width, 640.0f);
  EXPECT_FLOAT_EQ(snapshot.view.height, 360.0f);
}

TEST(InputView, NoCameraLeavesTheCameraEmptyAndAClearedOneReplacesAnEarlierCamera)
{
  const auto camera = uuids::uuid::from_string("0b0d0f6c-3f1c-4f3b-9d56-0d6f4b6a1c11");

  input::InputSnapshot snapshot;
  input::setView(snapshot, 10.0f, 10.0f, { 0.0f, 0.0f, 100.0f, 100.0f }, camera);
  ASSERT_FALSE(snapshot.view.camera.empty());

  input::setView(snapshot, -20.0f, 10.0f, { 0.0f, 0.0f, 100.0f, 100.0f }, std::nullopt);
  EXPECT_TRUE(snapshot.view.camera.empty());
  EXPECT_FLOAT_EQ(snapshot.view.cursorX, -20.0f);
}
