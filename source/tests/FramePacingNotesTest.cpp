#include <FramePacingNotes.h>
#include <gtest/gtest.h>

TEST(FramePacingNotesTest, FallbackNoteShownWhenOffRequestedAndPresentWaits)
{
  const auto notes = framePacingNotes(false, true, 0);

  EXPECT_TRUE(notes.vsyncOffUnavailable);
}

TEST(FramePacingNotesTest, FallbackNoteAbsentWhenOffIsHonored)
{
  EXPECT_FALSE(framePacingNotes(false, false, 0).vsyncOffUnavailable);
}

TEST(FramePacingNotesTest, FallbackNoteAbsentWhenVsyncRequested)
{
  EXPECT_FALSE(framePacingNotes(true, true, 0).vsyncOffUnavailable);
  EXPECT_FALSE(framePacingNotes(true, false, 0).vsyncOffUnavailable);
}

TEST(FramePacingNotesTest, CapNoteShownOnlyForVsyncOnWithCap)
{
  EXPECT_TRUE(framePacingNotes(true, true, 60).capAboveRefreshIgnored);
  EXPECT_FALSE(framePacingNotes(true, true, 0).capAboveRefreshIgnored);
  EXPECT_FALSE(framePacingNotes(false, false, 60).capAboveRefreshIgnored);
  EXPECT_FALSE(framePacingNotes(false, true, 60).capAboveRefreshIgnored);
}

TEST(FramePacingNotesTest, UnlimitedWithVsyncOffShowsNoCapNote)
{
  const auto notes = framePacingNotes(false, false, 0);

  EXPECT_FALSE(notes.vsyncOffUnavailable);
  EXPECT_FALSE(notes.capAboveRefreshIgnored);
}
