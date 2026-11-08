#include <FramePacingNotes.h>
#include <gtest/gtest.h>

TEST(FramePacingNotesTest, FallbackNoteShownWhenOffRequestedAndSurfaceHasNoNonBlockingMode)
{
  EXPECT_TRUE(framePacingNotes(false, false, false, 0).vsyncOffUnavailable);
}

TEST(FramePacingNotesTest, FallbackNoteAbsentWhenSurfaceOffersImmediate)
{
  EXPECT_FALSE(framePacingNotes(false, true, false, 0).vsyncOffUnavailable);
}

TEST(FramePacingNotesTest, FallbackNoteAbsentWhenSurfaceOffersOnlyMailbox)
{
  EXPECT_FALSE(framePacingNotes(false, false, true, 0).vsyncOffUnavailable);
  EXPECT_FALSE(framePacingNotes(false, true, true, 0).vsyncOffUnavailable);
}

TEST(FramePacingNotesTest, FallbackNoteAbsentWhenVsyncRequested)
{
  EXPECT_FALSE(framePacingNotes(true, false, false, 0).vsyncOffUnavailable);
  EXPECT_FALSE(framePacingNotes(true, true, true, 0).vsyncOffUnavailable);
}

TEST(FramePacingNotesTest, CapNoteShownOnlyForVsyncOnWithCap)
{
  EXPECT_TRUE(framePacingNotes(true, false, false, 60).capAboveRefreshIgnored);
  EXPECT_FALSE(framePacingNotes(true, false, false, 0).capAboveRefreshIgnored);
  EXPECT_FALSE(framePacingNotes(false, true, true, 60).capAboveRefreshIgnored);
  EXPECT_FALSE(framePacingNotes(false, false, false, 60).capAboveRefreshIgnored);
}

TEST(FramePacingNotesTest, UnlimitedWithVsyncOffShowsNoCapNote)
{
  const auto notes = framePacingNotes(false, true, true, 0);

  EXPECT_FALSE(notes.vsyncOffUnavailable);
  EXPECT_FALSE(notes.capAboveRefreshIgnored);
}
