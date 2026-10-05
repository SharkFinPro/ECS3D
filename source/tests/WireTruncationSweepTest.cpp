// Cuts every wire payload at every offset. The sanitize CI rows run this under ASan/UBSan, which is what
// turns a read past the end of a short payload into a failure rather than a lucky pass.
#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "WireTruncation.h"
#include "WireTruncationFixtures.h"
#include "ServerLog.h"

#include <LogEntry.h>
#include <Protocol.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {
  using fixtures::Scene;
  using fixtures::makeScene;
  using wiretest::buildResidentProject;
  using wiretest::buildSnapshotProject;
  using wiretest::buildTree;
  using wiretest::canonical;
  using wiretest::copyOf;
  using wiretest::forEachProperPrefix;
  using wiretest::makeProject;
  using wiretest::makeResidentScene;
  using wiretest::packInput;
  using wiretest::stateOf;
  using wiretest::uuidFrom;

  // The positions the delta test's objects held before the delta moved them: 1, 2, 3 on x, in order.
  void resetToOriginalPositions(const Scene& scene, const std::vector<std::shared_ptr<Object>>& order)
  {
    for (std::size_t i = 0; i < order.size(); ++i)
    {
      const auto object = scene.objectManager->getObjectByUUID(order[i]->getUUID());
      fixtures::transformOf(object)->setPosition({ 1.0f + static_cast<float>(i), 0.0f, 0.0f });
    }
  }
}


TEST(WireTruncationSweep, EveryPrefixOfASnapshotIsRefusedAndLeavesTheProjectIntact)
{
  const auto source = makeProject();
  buildSnapshotProject(source);

  net::Message snapshot(net::MessageType::snapshot);
  source.packer->pack(snapshot);

  const auto target = makeProject();
  buildResidentProject(target);
  const auto before = canonical(target.serializer->serialize());

  forEachProperPrefix(snapshot, [&](const net::Message& prefix, std::size_t) {
    EXPECT_ANY_THROW(target.packer->unpack(prefix));
    EXPECT_EQ(canonical(target.serializer->serialize()), before);
  });

  target.packer->unpack(snapshot);
  EXPECT_NE(canonical(target.serializer->serialize()), before);
  EXPECT_EQ(canonical(target.serializer->serialize()), canonical(source.serializer->serialize()));
}

TEST(WireTruncationSweep, EveryPrefixOfAStateDeltaIsRefusedAndAppliesOnlyWholeEntries)
{
  const auto source = makeScene();
  const auto first = addObject(source, "First", { 1.0f, 0.0f, 0.0f });
  const auto second = addObject(source, "Second", { 2.0f, 0.0f, 0.0f });
  const auto third = addChildObject(source, "Third", second);
  fixtures::transformOf(third)->setPosition({ 3.0f, 0.0f, 0.0f });

  const auto target = copyOf(source);

  // Moved only after the target copy, so the delta carries values the target does not have yet.
  fixtures::transformOf(first)->setPosition({ 11.0f, 5.0f, 5.0f });
  fixtures::transformOf(second)->setPosition({ 12.0f, 5.0f, 5.0f });
  fixtures::transformOf(third)->setPosition({ 13.0f, 5.0f, 5.0f });

  net::Message delta(net::MessageType::stateDelta);
  replication::packStateDelta(delta, *source.objectManager);

  // The count, then per entry: a length-prefixed 36 character uuid and three vec3.
  constexpr std::size_t entryBytes = sizeof(uint32_t) + 36 + 3 * sizeof(glm::vec3);
  ASSERT_EQ(delta.size(), sizeof(uint32_t) + 3 * entryBytes);

  const std::vector<std::shared_ptr<Object>> sourceOrder = source.objectManager->getAllObjects();

  forEachProperPrefix(delta, [&](const net::Message& prefix, const std::size_t length) {
    const auto fresh = copyOf(source);
    resetToOriginalPositions(fresh, sourceOrder);

    EXPECT_ANY_THROW(replication::unpackStateDelta(*fresh.objectManager, prefix));

    // Each entry is read whole before it is applied, so exactly the entries that fit are applied.
    for (std::size_t i = 0; i < sourceOrder.size(); ++i)
    {
      const auto object = fresh.objectManager->getObjectByUUID(sourceOrder[i]->getUUID());
      const bool entryFit = length >= sizeof(uint32_t) + (i + 1) * entryBytes;
      const glm::vec3 expected = entryFit ? fixtures::transformOf(sourceOrder[i])->getLocalPosition()
                                          : glm::vec3(1.0f + static_cast<float>(i), 0.0f, 0.0f);

      EXPECT_EQ(fixtures::transformOf(object)->getLocalPosition(), expected) << "entry " << i;
    }
  });

  replication::unpackStateDelta(*target.objectManager, delta);
  EXPECT_EQ(fixtures::transformOf(target.objectManager->getObjectByUUID(third->getUUID()))->getLocalPosition(),
            glm::vec3(13.0f, 5.0f, 5.0f));
}

TEST(WireTruncationSweep, EveryPrefixOfAnObjectSpawnedIsRefusedAndLeavesTheSceneIntact)
{
  const auto source = makeScene();
  const auto message = replication::buildObjectSpawned(*buildTree(source));

  const auto target = makeResidentScene();
  const auto before = stateOf(target);
  const auto objectCount = target.objectManager->getAllObjects().size();

  forEachProperPrefix(message, [&](const net::Message& prefix, std::size_t) {
    EXPECT_ANY_THROW(replication::applyObjectSpawned(*target.objectManager, prefix));
    EXPECT_EQ(stateOf(target), before);
    EXPECT_EQ(target.objectManager->getAllObjects().size(), objectCount);
  });

  replication::applyObjectSpawned(*target.objectManager, message);
  EXPECT_EQ(target.objectManager->getAllObjects().size(), objectCount + 3);
  EXPECT_NE(stateOf(target), before);
}

TEST(WireTruncationSweep, ARefusedObjectSpawnedLeavesNoUuidBehindInTheScene)
{
  const auto source = makeScene();
  const auto message = replication::buildObjectSpawned(*buildTree(source));
  const auto spawned = source.objectManager->getAllObjects();

  const auto target = makeResidentScene();

  forEachProperPrefix(message, [&](const net::Message& prefix, std::size_t) {
    EXPECT_ANY_THROW(replication::applyObjectSpawned(*target.objectManager, prefix));
  });

  ASSERT_EQ(spawned.size(), 3u);
  for (const auto& object : spawned)
  {
    EXPECT_EQ(target.objectManager->getObjectByUUID(object->getUUID()), nullptr);
  }

  replication::applyObjectSpawned(*target.objectManager, message);
  for (const auto& object : spawned)
  {
    EXPECT_NE(target.objectManager->getObjectByUUID(object->getUUID()), nullptr);
  }
}

TEST(WireTruncationSweep, EveryPrefixOfAnObjectDestroyedIsRefusedAndLeavesTheSceneIntact)
{
  const auto target = makeResidentScene();
  const auto victim = buildTree(target);
  const auto before = stateOf(target);

  const auto message = replication::buildObjectDestroyed(victim->getUUID());

  forEachProperPrefix(message, [&](const net::Message& prefix, std::size_t) {
    EXPECT_ANY_THROW(replication::applyObjectDestroyed(*target.objectManager, prefix));
    EXPECT_EQ(stateOf(target), before);
  });

  replication::applyObjectDestroyed(*target.objectManager, message);
  EXPECT_NE(stateOf(target), before);
  EXPECT_EQ(target.objectManager->getObjectByUUID(victim->getUUID()), nullptr);
}

TEST(WireTruncationSweep, EveryPrefixOfAnObjectComponentsChangedIsRefused)
{
  const auto source = makeScene();
  const auto parent = buildTree(source);
  const auto unchanged = copyOf(source);
  const auto before = stateOf(unchanged);

  fixtures::addRigidBody(parent);
  addChildObject(source, "New Child", parent);

  const auto message = replication::buildObjectComponentsChanged(*parent);

  // An object that is unpacked in place stays as far along as the payload got - documented - so only
  // a cut inside the leading uuid, which is read before anything is touched, promises no change.
  constexpr std::size_t uuidBytes = sizeof(uint32_t) + 36;

  forEachProperPrefix(message, [&](const net::Message& prefix, const std::size_t length) {
    const auto target = copyOf(unchanged);

    EXPECT_ANY_THROW(replication::applyObjectComponentsChanged(*target.objectManager, prefix));

    if (length < uuidBytes)
    {
      EXPECT_EQ(stateOf(target), before);
    }
  });

  const auto target = copyOf(unchanged);
  replication::applyObjectComponentsChanged(*target.objectManager, message);
  EXPECT_NE(stateOf(target), before);
  EXPECT_EQ(stateOf(target), stateOf(source));
}

namespace {
  // Every prefix of a component edit is refused. One that ends before the component body has started is a
  // malformed payload and changes nothing; one inside the body is partiallyApplied, which a component
  // that unpacks field by field cannot avoid, unless it parses before it assigns (unchangedInBody).
  void expectEveryPrefixRefused(const Scene& scene, const net::Message& edit, const std::size_t bodyStart,
                                const bool unchangedInBody)
  {
    const auto before = stateOf(scene);

    forEachProperPrefix(edit, [&](const net::Message& prefix, const std::size_t length) {
      const auto result = replication::applyComponentEdit(*scene.objectManager, prefix);

      if (length < bodyStart)
      {
        EXPECT_EQ(result, replication::ComponentEditResult::malformedPayload);
        EXPECT_EQ(stateOf(scene), before);
      }
      else
      {
        EXPECT_EQ(result, replication::ComponentEditResult::partiallyApplied);

        if (unchangedInBody)
        {
          EXPECT_EQ(stateOf(scene), before);
        }
      }
    });
  }
}

TEST(WireTruncationSweep, EveryPrefixOfATransformEditIsRefused)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Edited", { 1.0f, 2.0f, 3.0f });
  const auto transform = fixtures::transformOf(object);

  transform->setPosition({ 7.0f, 8.0f, 9.0f });
  transform->setScale({ 2.0f, 2.0f, 2.0f });
  const auto edit = replication::buildComponentEdit(object->getUUID(), transform);
  const auto fullState = stateOf(scene);
  transform->setPosition({ 1.0f, 2.0f, 3.0f });
  transform->setScale({ 1.0f, 1.0f, 1.0f });

  const auto headerBytes = sizeof(uint32_t) + 36 + sizeof(ComponentType);

  // A fresh scene per cut is not needed for the malformed cuts, but the partial ones do write the
  // transform, so each prefix runs against the reset values.
  const auto before = stateOf(scene);
  forEachProperPrefix(edit, [&](const net::Message& prefix, const std::size_t length) {
    transform->setPosition({ 1.0f, 2.0f, 3.0f });
    transform->setRotation({ 0.0f, 0.0f, 0.0f });
    transform->setScale({ 1.0f, 1.0f, 1.0f });

    const auto result = replication::applyComponentEdit(*scene.objectManager, prefix);

    if (length < headerBytes)
    {
      EXPECT_EQ(result, replication::ComponentEditResult::malformedPayload);
      EXPECT_EQ(stateOf(scene), before);
    }
    else
    {
      EXPECT_EQ(result, replication::ComponentEditResult::partiallyApplied);
    }
  });

  transform->setPosition({ 1.0f, 2.0f, 3.0f });
  transform->setRotation({ 0.0f, 0.0f, 0.0f });
  transform->setScale({ 1.0f, 1.0f, 1.0f });
  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::applied);
  EXPECT_EQ(stateOf(scene), fullState);
}

TEST(WireTruncationSweep, EveryPrefixOfAScriptEditIsRefusedAndChangesNothing)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Scripted");

  const auto script = std::make_shared<Script>();
  script->setClassName("SweepScript");
  script->setFields(nlohmann::json{ { "speed", 1.0 } });
  object->addComponent(script);

  const auto original = script->getFields();
  script->setFields(nlohmann::json{ { "speed", 4.5 }, { "label", "hello there" } });
  const auto edit = replication::buildComponentEdit(object->getUUID(), script);
  const auto edited = stateOf(scene);
  script->setFields(original);
  const auto before = stateOf(scene);

  const std::size_t bodyStart = sizeof(uint32_t) + 36 + sizeof(ComponentType) + sizeof(uint32_t) +
                               std::string("SweepScript").size();
  expectEveryPrefixRefused(scene, edit, bodyStart, true);
  EXPECT_EQ(stateOf(scene), before);

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::applied);
  EXPECT_EQ(stateOf(scene), edited);
}

TEST(WireTruncationSweep, EveryPrefixOfASceneEditIsNotParsedAndLeavesTheSceneIntact)
{
  const auto scene = makeResidentScene();
  const auto before = stateOf(scene);

  const auto edit = replication::buildBatch({ replication::buildAddObject("Added One"),
                                              replication::buildAddObject("Added Two") });
  const auto payload = edit.dump();

  net::Message message(net::MessageType::sceneEdit);
  for (const char character : payload)
  {
    message.write(static_cast<uint8_t>(character));
  }

  forEachProperPrefix(message, [&](const net::Message& prefix, std::size_t) {
    const auto parsed = replication::parseSceneEditMessage(prefix);
    EXPECT_FALSE(parsed.has_value());

    if (parsed)
    {
      replication::applySceneEdit(*scene.objectManager, *parsed);
    }

    EXPECT_EQ(stateOf(scene), before);
  });

  const auto parsed = replication::parseSceneEditMessage(message);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *parsed), replication::SceneEditResult::applied);
  EXPECT_NE(stateOf(scene), before);
}

TEST(WireTruncationSweep, EveryPrefixOfAnInputStateIsRefusedOrDegradesToNoMouse)
{
  const std::vector<int> keys{ 65, 68, 87 };
  const auto message = packInput(keys);

  const auto full = replication::parseInputState(message);
  ASSERT_TRUE(full.has_value());
  EXPECT_TRUE(full->hasMouse);
  EXPECT_EQ(full->keysPressed, keys);

  // bool + key count, then the key codes, then the optional mouse block.
  constexpr std::size_t countEnd = sizeof(uint8_t) + sizeof(uint32_t);
  const std::size_t keysEnd = countEnd + keys.size() * sizeof(int32_t);

  forEachProperPrefix(message, [&](const net::Message& prefix, const std::size_t length) {
    if (length < countEnd)
    {
      EXPECT_ANY_THROW(static_cast<void>(replication::parseInputState(prefix)));
      return;
    }

    const auto parsed = replication::parseInputState(prefix);

    if (length < keysEnd)
    {
      EXPECT_FALSE(parsed.has_value());
      return;
    }

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->keysPressed, keys);
    EXPECT_FALSE(parsed->hasMouse);
  });
}

TEST(WireTruncationSweep, EveryPrefixOfAServerLogIsRefused)
{
  const auto base = std::chrono::system_clock::time_point(std::chrono::milliseconds(1700000000000));

  const std::vector<LogEntry> entries{
    { base, LogLevel::info, LogCategory::server, "Server started" },
    { base + std::chrono::milliseconds(5), LogLevel::warn, LogCategory::script, "A script said something" },
    { base + std::chrono::milliseconds(9), LogLevel::error, LogCategory::net, "" }
  };
  const auto message = net::packServerLog(entries, 7);

  forEachProperPrefix(message, [&](const net::Message& prefix, std::size_t) {
    EXPECT_ANY_THROW(static_cast<void>(net::unpackServerLog(prefix)));
  });

  const auto batch = net::unpackServerLog(message);
  EXPECT_EQ(batch.dropped, 7u);
  ASSERT_EQ(batch.entries.size(), entries.size());
  EXPECT_EQ(batch.entries[1].message, "A script said something");
  EXPECT_EQ(batch.entries[1].time, entries[1].time);
}

TEST(WireTruncationSweep, EveryPrefixOfAnAddAssetIsRefusedExceptTheOneWithoutADisplayName)
{
  const nlohmann::json asset = {
    { "assetType", "prefab" },
    { "uuid", "44444444-4444-4444-4444-444444444444" },
    { "path", "Block" },
    { "name", "Block" },
    { "className", "" },
    { "body", "{\"name\":\"Block\"}" },
    { "displayName", "Fancy Block" }
  };
  const auto message = replication::packAddAsset(asset);

  // displayName was appended after the original six fields, so a payload that stops exactly before it
  // is the older shape and unpacks with an empty one.
  const std::size_t withoutDisplayName = message.size() - (sizeof(uint32_t) + std::string("Fancy Block").size());

  forEachProperPrefix(message, [&](const net::Message& prefix, const std::size_t length) {
    if (length == withoutDisplayName)
    {
      const auto parsed = replication::unpackAddAsset(prefix);
      EXPECT_EQ(parsed.at("displayName").get<std::string>(), "");
      EXPECT_EQ(parsed.at("body").get<std::string>(), "{\"name\":\"Block\"}");
      return;
    }

    EXPECT_ANY_THROW(static_cast<void>(replication::unpackAddAsset(prefix)));
  });

  const auto parsed = replication::unpackAddAsset(message);
  EXPECT_EQ(parsed.at("displayName").get<std::string>(), "Fancy Block");
  EXPECT_EQ(parsed.at("uuid").get<std::string>(), "44444444-4444-4444-4444-444444444444");
}

TEST(WireTruncationSweep, EveryPrefixOfARenameAssetIsRefused)
{
  const auto message = replication::packRenameAsset(
    replication::buildRenameAsset(uuidFrom("22222222-2222-2222-2222-222222222222"), "Nicer Wood"));

  forEachProperPrefix(message, [&](const net::Message& prefix, std::size_t) {
    EXPECT_ANY_THROW(static_cast<void>(replication::unpackRenameAsset(prefix)));
  });

  const auto parsed = replication::unpackRenameAsset(message);
  EXPECT_EQ(parsed.at("displayName").get<std::string>(), "Nicer Wood");
}

TEST(WireTruncationSweep, EveryPrefixOfARemoveAssetIsRefused)
{
  const auto message = replication::packRemoveAsset(
    replication::buildRemoveAsset(uuidFrom("22222222-2222-2222-2222-222222222222")));

  forEachProperPrefix(message, [&](const net::Message& prefix, std::size_t) {
    EXPECT_ANY_THROW(static_cast<void>(replication::unpackRemoveAsset(prefix)));
  });

  const auto parsed = replication::unpackRemoveAsset(message);
  EXPECT_EQ(parsed.at("uuid").get<std::string>(), "22222222-2222-2222-2222-222222222222");
}
