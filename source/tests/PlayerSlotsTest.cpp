#include <gtest/gtest.h>

#include "TestScene.h"
#include "PlayerSlots.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Camera.h"
#include "objects/components/PlayerController.h"

#include <Protocol.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace {
  void addController(const std::shared_ptr<Object>& object, const int32_t slot)
  {
    const auto controller = std::make_shared<PlayerController>();
    controller->setPlayerSlot(slot);
    object->addComponent(controller);
  }

  void addCamera(const std::shared_ptr<Object>& object)
  {
    object->addComponent(std::make_shared<Camera>());
  }
}

TEST(PlayerSlotTable, AssignsTheLowestFreeSlotAndIsIdempotent)
{
  PlayerSlotTable table;

  EXPECT_EQ(table.assign(10), 0);
  EXPECT_EQ(table.assign(11), 1);
  EXPECT_EQ(table.assign(12), 2);

  EXPECT_EQ(table.assign(11), 1);
  EXPECT_EQ(table.assign(10), 0);
  EXPECT_EQ(table.slotOf(12), 2);
}

TEST(PlayerSlotTable, ReleaseFreesTheSlotForReuse)
{
  PlayerSlotTable table;
  table.assign(10);
  table.assign(11);
  table.assign(12);

  EXPECT_EQ(table.release(11), 1);
  EXPECT_FALSE(table.slotOf(11).has_value());
  EXPECT_EQ(table.assign(13), 1);

  EXPECT_FALSE(table.release(99).has_value());
  EXPECT_EQ(table.slotOf(10), 0);
}

TEST(PlayerSlotTable, RequestingAFreeSlotMovesTheConnectionAndFreesItsOldSlot)
{
  PlayerSlotTable table;
  table.assign(10);
  table.assign(11);

  const auto outcome = table.request(10, 5);

  EXPECT_EQ(outcome.result, PossessResult::granted);
  EXPECT_EQ(outcome.previousSlot, 0);
  EXPECT_EQ(table.slotOf(10), 5);
  EXPECT_EQ(table.slotOf(11), 1);

  EXPECT_EQ(table.assign(12), 0);
}

TEST(PlayerSlotTable, RequestingASlotAnotherConnectionHoldsIsRefusedAndChangesNothing)
{
  PlayerSlotTable table;
  table.assign(10);
  table.assign(11);

  const auto outcome = table.request(10, 1);

  EXPECT_EQ(outcome.result, PossessResult::held);
  EXPECT_FALSE(outcome.previousSlot.has_value());
  EXPECT_EQ(table.slotOf(10), 0);
  EXPECT_EQ(table.slotOf(11), 1);

  table.release(11);
  const auto retry = table.request(10, 1);
  EXPECT_EQ(retry.result, PossessResult::granted);
  EXPECT_EQ(table.slotOf(10), 1);
}

TEST(PlayerSlotTable, RequestingYourOwnSlotIsUnchanged)
{
  PlayerSlotTable table;
  table.assign(10);

  const auto outcome = table.request(10, 0);

  EXPECT_EQ(outcome.result, PossessResult::unchanged);
  EXPECT_EQ(table.slotOf(10), 0);
}

TEST(PlayerSlotTable, SlotsOutsideTheValidRangeAreRefused)
{
  PlayerSlotTable table;
  table.assign(10);

  EXPECT_EQ(table.request(10, -1).result, PossessResult::outOfRange);
  EXPECT_EQ(table.request(10, maxPlayerSlot + 1).result, PossessResult::outOfRange);
  EXPECT_EQ(table.slotOf(10), 0);

  EXPECT_EQ(table.request(10, maxPlayerSlot).result, PossessResult::granted);
  EXPECT_EQ(table.slotOf(10), maxPlayerSlot);
}

TEST(PlayerSlotTable, ARequestFromAnUnassignedConnectionBindsTheRequestedSlot)
{
  PlayerSlotTable table;
  table.assign(10);

  const auto outcome = table.request(20, 3);

  EXPECT_EQ(outcome.result, PossessResult::granted);
  EXPECT_FALSE(outcome.previousSlot.has_value());
  EXPECT_EQ(table.slotOf(20), 3);

  EXPECT_EQ(table.request(21, 0).result, PossessResult::held);
  EXPECT_FALSE(table.slotOf(21).has_value());
}

TEST(PlayerSlotsInScene, ListsEverySlotSortedAndWithoutDuplicatesIncludingChildren)
{
  const auto scene = fixtures::makeScene();
  const auto a = fixtures::addObject(scene, "A");
  const auto b = fixtures::addObject(scene, "B");
  const auto c = fixtures::addObject(scene, "C");
  const auto child = fixtures::addChildObject(scene, "Child", a);
  addController(a, 4);
  addController(b, 1);
  addController(c, 4);
  addController(child, 2);

  EXPECT_EQ(playerSlotsInScene(*scene.objectManager), (std::vector<int32_t>{ 1, 2, 4 }));
}

TEST(PlayerSlotsInScene, IsEmptyWithoutAnyPlayerController)
{
  const auto scene = fixtures::makeScene();
  fixtures::addObject(scene, "A");

  EXPECT_TRUE(playerSlotsInScene(*scene.objectManager).empty());

  addController(fixtures::addObject(scene, "B"), 0);
  EXPECT_EQ(playerSlotsInScene(*scene.objectManager), (std::vector<int32_t>{ 0 }));
}

TEST(FindPlayerCamera, FindsTheObjectWithTheSlotAndACamera)
{
  const auto scene = fixtures::makeScene();
  const auto player = fixtures::addObject(scene, "Player");
  addController(player, 2);
  addCamera(player);

  EXPECT_EQ(findPlayerCamera(*scene.objectManager, 2), player->getUUID());
}

TEST(FindPlayerCamera, SkipsAControllerWithoutACameraAndFindsALaterOneThatHasIt)
{
  const auto scene = fixtures::makeScene();
  const auto noCamera = fixtures::addObject(scene, "NoCamera");
  addController(noCamera, 2);
  EXPECT_FALSE(findPlayerCamera(*scene.objectManager, 2).has_value());

  const auto withCamera = fixtures::addObject(scene, "WithCamera");
  addController(withCamera, 2);
  addCamera(withCamera);

  EXPECT_EQ(findPlayerCamera(*scene.objectManager, 2), withCamera->getUUID());
}

TEST(FindPlayerCamera, IsEmptyForAMissingSlotOrACameraOnAnotherSlot)
{
  const auto scene = fixtures::makeScene();
  const auto player = fixtures::addObject(scene, "Player");
  addController(player, 1);
  addCamera(player);

  EXPECT_FALSE(findPlayerCamera(*scene.objectManager, 7).has_value());
  EXPECT_TRUE(findPlayerCamera(*scene.objectManager, 1).has_value());
}

TEST(PlayerSlotPayloads, PossessSlotRoundTrips)
{
  const auto message = replication::buildPossessSlot(37);

  EXPECT_EQ(message.getType(), net::MessageType::possessSlot);
  EXPECT_EQ(replication::parsePossessSlot(message), 37);
}

TEST(PlayerSlotPayloads, PlayerSlotRoundTrips)
{
  const auto message = replication::buildPlayerSlot(0x1122334455667788ull, 9);

  EXPECT_EQ(message.getType(), net::MessageType::playerSlot);
  const auto payload = replication::parsePlayerSlot(message);
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(payload->nonce, 0x1122334455667788ull);
  EXPECT_EQ(payload->slot, 9);
}

TEST(PlayerSlotPayloads, TruncatedPayloadsParseToNothing)
{
  EXPECT_FALSE(replication::parsePossessSlot(net::Message(net::MessageType::possessSlot)).has_value());

  net::Message shortPossess(net::MessageType::possessSlot);
  shortPossess.write<uint16_t>(1);
  EXPECT_FALSE(replication::parsePossessSlot(shortPossess).has_value());

  net::Message nonceOnly(net::MessageType::playerSlot);
  nonceOnly.write<uint64_t>(5);
  EXPECT_FALSE(replication::parsePlayerSlot(nonceOnly).has_value());
  EXPECT_FALSE(replication::parsePlayerSlot(net::Message(net::MessageType::playerSlot)).has_value());
}
