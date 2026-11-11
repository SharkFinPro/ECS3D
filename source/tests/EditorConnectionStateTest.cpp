#include <gtest/gtest.h>

#include "EditorConnectionState.h"
#include <cstring>

TEST(EditorConnectionStateTest, EditableServerIsConnectedWithNoBadge)
{
  const auto state = editorConnectionForEditStatus(true);

  EXPECT_EQ(state, EditorConnectionState::connected);
  EXPECT_EQ(editorConnectionBadge(state), nullptr);
  EXPECT_EQ(editorConnectionPanelNotice(state), nullptr);
}

TEST(EditorConnectionStateTest, NonEditServerKeepsTheReadOnlyWording)
{
  const auto state = editorConnectionForEditStatus(false);

  EXPECT_EQ(state, EditorConnectionState::readOnlyServer);
  EXPECT_STREQ(editorConnectionBadge(state), "READ-ONLY (server not in edit mode)");
  EXPECT_STREQ(editorConnectionPanelNotice(state), "Read-only - server is not in edit mode");
}

TEST(EditorConnectionStateTest, DropAfterASessionReadsAsDisconnected)
{
  const auto state = editorConnectionAfterLoss(true, true);

  EXPECT_EQ(state, EditorConnectionState::connectionLost);
  EXPECT_STREQ(editorConnectionBadge(state), "Disconnected from server");
  EXPECT_STREQ(editorConnectionPanelNotice(state), "Read-only - disconnected from the server");
  EXPECT_EQ(std::strstr(editorConnectionBadge(state), "not in edit mode"), nullptr);
}

TEST(EditorConnectionStateTest, DropBeforeAnyServerMessageWithATokenReadsAsRejected)
{
  const auto state = editorConnectionAfterLoss(false, true);

  EXPECT_EQ(state, EditorConnectionState::editTokenRejected);
  EXPECT_NE(std::strstr(editorConnectionBadge(state), "Edit token rejected"), nullptr);
  EXPECT_NE(std::strstr(editorConnectionPanelNotice(state), "edit token rejected"), nullptr);
  EXPECT_EQ(std::strstr(editorConnectionBadge(state), "not in edit mode"), nullptr);
}

TEST(EditorConnectionStateTest, DropBeforeAnyServerMessageWithoutAnExternalTokenIsNotARejection)
{
  EXPECT_EQ(editorConnectionAfterLoss(false, false), EditorConnectionState::connectionLost);
}

TEST(EditorConnectionStateTest, SpawnedServerTokenIsNeverExternal)
{
  EXPECT_FALSE(editorHasExternalServerToken(true, true));
  EXPECT_TRUE(editorHasExternalServerToken(false, true));
  EXPECT_FALSE(editorHasExternalServerToken(false, false));

  const auto spawnedServerDies = editorConnectionAfterLoss(false, editorHasExternalServerToken(true, true));
  EXPECT_EQ(spawnedServerDies, EditorConnectionState::connectionLost);

  const auto externalRejects = editorConnectionAfterLoss(false, editorHasExternalServerToken(false, true));
  EXPECT_EQ(externalRejects, EditorConnectionState::editTokenRejected);
}

TEST(EditorConnectionStateTest, EveryNonConnectedStateHasAUniqueBadgeAndNotice)
{
  const EditorConnectionState states[] = {EditorConnectionState::readOnlyServer, EditorConnectionState::connectionLost,
                                          EditorConnectionState::editTokenRejected};

  for (const auto a : states)
  {
    ASSERT_NE(editorConnectionBadge(a), nullptr);
    ASSERT_NE(editorConnectionPanelNotice(a), nullptr);

    for (const auto b : states)
    {
      if (a != b)
      {
        EXPECT_STRNE(editorConnectionBadge(a), editorConnectionBadge(b));
        EXPECT_STRNE(editorConnectionPanelNotice(a), editorConnectionPanelNotice(b));
      }
    }
  }
}

TEST(EditorConnectionStateTest, LogMessagesNameTheCause)
{
  EXPECT_NE(std::strstr(editorConnectionLogMessage(EditorConnectionState::editTokenRejected), "token"), nullptr);
  EXPECT_NE(std::strstr(editorConnectionLogMessage(EditorConnectionState::connectionLost), "was lost"), nullptr);
  EXPECT_NE(editorConnectionLogMessage(EditorConnectionState::connected), nullptr);
  EXPECT_NE(editorConnectionLogMessage(EditorConnectionState::readOnlyServer), nullptr);
}
