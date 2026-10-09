#include <gtest/gtest.h>

#include "UnresolvedScripts.h"

#include <objects/components/Script.h>

#include <memory>
#include <string>
#include <uuid.h>

namespace {
  uuids::uuid makeUUID(const char* text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const uuids::uuid objectA = makeUUID("00000000-0000-0000-0000-00000000000a");
  const uuids::uuid objectB = makeUUID("00000000-0000-0000-0000-00000000000b");
}

TEST(UnresolvedScriptsTest, StartsEmptyAndResolvesNothing)
{
  const UnresolvedScripts unresolved;
  const auto script = std::make_shared<Script>("Mover");

  EXPECT_TRUE(unresolved.empty());
  EXPECT_FALSE(unresolved.isUnresolved(objectA, "Mover", script.get()));
}

TEST(UnresolvedScriptsTest, AddRecordsThePairForThatComponentOnly)
{
  UnresolvedScripts unresolved;
  const auto script = std::make_shared<Script>("Mover");
  const auto other = std::make_shared<Script>("Mover");

  EXPECT_TRUE(unresolved.add(objectA, "Mover", script));

  EXPECT_TRUE(unresolved.isUnresolved(objectA, "Mover", script.get()));
  EXPECT_FALSE(unresolved.isUnresolved(objectA, "Mover", other.get()));
  EXPECT_FALSE(unresolved.isUnresolved(objectA, "Spinner", script.get()));
  EXPECT_FALSE(unresolved.isUnresolved(objectB, "Mover", script.get()));
  EXPECT_FALSE(unresolved.empty());
}

TEST(UnresolvedScriptsTest, AddingTheSameComponentAgainReportsNoNewFailure)
{
  UnresolvedScripts unresolved;
  const auto script = std::make_shared<Script>("Mover");
  const auto replacement = std::make_shared<Script>("Mover");

  EXPECT_TRUE(unresolved.add(objectA, "Mover", script));
  EXPECT_FALSE(unresolved.add(objectA, "Mover", script));

  EXPECT_TRUE(unresolved.add(objectA, "Mover", replacement));
  EXPECT_TRUE(unresolved.isUnresolved(objectA, "Mover", replacement.get()));
  EXPECT_FALSE(unresolved.isUnresolved(objectA, "Mover", script.get()));
}

TEST(UnresolvedScriptsTest, PruneKeepsOnlyEntriesWhoseComponentIsStillLive)
{
  UnresolvedScripts unresolved;
  const auto kept = std::make_shared<Script>("Mover");
  const auto replaced = std::make_shared<Script>("Spinner");
  const auto removed = std::make_shared<Script>("Mover");
  const auto replacement = std::make_shared<Script>("Spinner");

  unresolved.add(objectA, "Mover", kept);
  unresolved.add(objectA, "Spinner", replaced);
  unresolved.add(objectB, "Mover", removed);

  unresolved.prune([&](const uuids::uuid& uuid, const std::string& className) -> const Component*
  {
    if (uuid == objectA && className == "Mover")
    {
      return kept.get();
    }

    if (uuid == objectA && className == "Spinner")
    {
      return replacement.get();
    }

    return nullptr;
  });

  EXPECT_TRUE(unresolved.isUnresolved(objectA, "Mover", kept.get()));
  EXPECT_FALSE(unresolved.isUnresolved(objectA, "Spinner", replaced.get()));
  EXPECT_FALSE(unresolved.isUnresolved(objectB, "Mover", removed.get()));
}

TEST(UnresolvedScriptsTest, PruneDropsAnEntryWhoseComponentWasDestroyed)
{
  UnresolvedScripts unresolved;
  auto script = std::make_shared<Script>("Mover");
  unresolved.add(objectA, "Mover", script);
  const auto* raw = script.get();
  script.reset();

  unresolved.prune([&](const uuids::uuid&, const std::string&) -> const Component* { return raw; });

  EXPECT_TRUE(unresolved.empty());
}

TEST(UnresolvedScriptsTest, ClearForgetsEverything)
{
  UnresolvedScripts unresolved;
  const auto script = std::make_shared<Script>("Mover");
  unresolved.add(objectA, "Mover", script);
  EXPECT_TRUE(unresolved.isUnresolved(objectA, "Mover", script.get()));

  unresolved.clear();

  EXPECT_TRUE(unresolved.empty());
  EXPECT_FALSE(unresolved.isUnresolved(objectA, "Mover", script.get()));
  EXPECT_TRUE(unresolved.add(objectA, "Mover", script));
}
