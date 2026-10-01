#include <gtest/gtest.h>

#include "AttachedScripts.h"

#include <uuid.h>

namespace {
  uuids::uuid makeUUID(const char* text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const uuids::uuid objectA = makeUUID("00000000-0000-0000-0000-00000000000a");
  const uuids::uuid objectB = makeUUID("00000000-0000-0000-0000-00000000000b");
}

TEST(AttachedScriptsTest, StartsEmpty)
{
  const AttachedScripts attached;

  EXPECT_TRUE(attached.empty());
  EXPECT_FALSE(attached.hasAnyAttached(objectA));
  EXPECT_FALSE(attached.isAttached(objectA, "Mover"));
}

TEST(AttachedScriptsTest, TwoClassesOnOneObjectKeepItAttachedUntilBothAreDetached)
{
  AttachedScripts attached;
  attached.attach(objectA, "Mover");
  attached.attach(objectA, "Spinner");
  EXPECT_TRUE(attached.hasAnyAttached(objectA));

  EXPECT_TRUE(attached.detach(objectA, "Mover"));
  EXPECT_TRUE(attached.hasAnyAttached(objectA));
  EXPECT_TRUE(attached.isAttached(objectA, "Spinner"));
  EXPECT_FALSE(attached.isAttached(objectA, "Mover"));

  EXPECT_TRUE(attached.detach(objectA, "Spinner"));
  EXPECT_FALSE(attached.hasAnyAttached(objectA));
  EXPECT_TRUE(attached.empty());
}

TEST(AttachedScriptsTest, IsAttachedIsPerUUIDAndClass)
{
  AttachedScripts attached;
  attached.attach(objectA, "Mover");

  EXPECT_TRUE(attached.isAttached(objectA, "Mover"));
  EXPECT_FALSE(attached.isAttached(objectA, "Spinner"));
  EXPECT_FALSE(attached.isAttached(objectB, "Mover"));
  EXPECT_TRUE(attached.hasAnyAttached(objectA));
  EXPECT_FALSE(attached.hasAnyAttached(objectB));
}

TEST(AttachedScriptsTest, ClearResetsEverything)
{
  AttachedScripts attached;
  attached.attach(objectA, "Mover");
  attached.attach(objectB, "Spinner");
  ASSERT_FALSE(attached.empty());

  attached.clear();

  EXPECT_TRUE(attached.empty());
  EXPECT_FALSE(attached.hasAnyAttached(objectA));
  EXPECT_FALSE(attached.hasAnyAttached(objectB));
  EXPECT_FALSE(attached.isAttached(objectA, "Mover"));
}

TEST(AttachedScriptsTest, DetachingAKeyNeverAttachedDoesNotUnderflow)
{
  AttachedScripts attached;
  attached.attach(objectA, "Mover");

  EXPECT_FALSE(attached.detach(objectA, "Spinner"));
  EXPECT_FALSE(attached.detach(objectB, "Mover"));

  EXPECT_TRUE(attached.hasAnyAttached(objectA));
  EXPECT_FALSE(attached.hasAnyAttached(objectB));
  EXPECT_TRUE(attached.detach(objectA, "Mover"));
  EXPECT_FALSE(attached.hasAnyAttached(objectA));
}

TEST(AttachedScriptsTest, ReattachingAnAttachedKeyDoesNotDoubleCount)
{
  AttachedScripts attached;
  EXPECT_TRUE(attached.attach(objectA, "Mover"));
  EXPECT_FALSE(attached.attach(objectA, "Mover"));

  EXPECT_TRUE(attached.detach(objectA, "Mover"));

  EXPECT_FALSE(attached.hasAnyAttached(objectA));
  EXPECT_FALSE(attached.isAttached(objectA, "Mover"));
}
