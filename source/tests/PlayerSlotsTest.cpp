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
#include <string>
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

TEST(PlayerSlotTable, AssignRefusesOnceEverySlotIsHeld)
{
  PlayerSlotTable table;
  for (int32_t conn = 0; conn <= maxPlayerSlot; ++conn)
  {
    ASSERT_EQ(table.assign(conn), conn);
  }

  const int32_t overflow = maxPlayerSlot + 1;
  EXPECT_FALSE(table.assign(overflow).has_value());
  EXPECT_FALSE(table.slotOf(overflow).has_value());
  EXPECT_EQ(table.heldSlots().size(), static_cast<size_t>(maxPlayerSlot + 1));

  EXPECT_EQ(table.assign(5), 5);
  EXPECT_EQ(table.assign(maxPlayerSlot), maxPlayerSlot);

  EXPECT_EQ(table.release(17), 17);
  EXPECT_EQ(table.assign(overflow), 17);
  EXPECT_EQ(table.slotOf(overflow), 17);
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

TEST(PlayerSlotsInScene, OmitsSlotsNoConnectionCouldBeBoundTo)
{
  const auto scene = fixtures::makeScene();
  addController(fixtures::addObject(scene, "Negative"), -1);
  addController(fixtures::addObject(scene, "Low"), 2);
  addController(fixtures::addObject(scene, "TooHigh"), maxPlayerSlot + 1);
  addController(fixtures::addObject(scene, "Highest"), maxPlayerSlot);

  EXPECT_EQ(playerSlotsInScene(*scene.objectManager), (std::vector<int32_t>{ 2, maxPlayerSlot }));
}

TEST(PlayerSlotTable, HeldSlotsListsEveryBoundSlotAscending)
{
  PlayerSlotTable table;
  EXPECT_TRUE(table.heldSlots().empty());

  // Connection ids descend while slots ascend, so hash-table order has no reason to come out sorted.
  for (int32_t connId = 90; connId >= 10; connId -= 10)
  {
    table.assign(connId);
  }

  EXPECT_EQ(table.heldSlots(), (std::vector<int32_t>{ 0, 1, 2, 3, 4, 5, 6, 7, 8 }));

  table.release(50);
  table.request(10, 20);
  EXPECT_EQ(table.heldSlots(), (std::vector<int32_t>{ 0, 1, 2, 3, 5, 6, 7, 20 }));
}

TEST(PermittedViewCamera, APlayerMayNameOnlyItsOwnSlotsCameraWhileAnEditorMayNameAny)
{
  const auto scene = fixtures::makeScene();
  const auto mine = fixtures::addObject(scene, "Mine");
  addController(mine, 1);
  addCamera(mine);
  const auto theirs = fixtures::addObject(scene, "Theirs");
  addController(theirs, 2);
  addCamera(theirs);
  const auto plain = fixtures::addObject(scene, "Plain");
  addCamera(plain);

  const std::string own = uuids::to_string(mine->getUUID());
  const std::string other = uuids::to_string(theirs->getUUID());
  const std::string unowned = uuids::to_string(plain->getUUID());

  // Positive controls: the slot's own camera passes, and an editor passes every one of these.
  EXPECT_EQ(permittedViewCamera(*scene.objectManager, 1, own, false), own);
  EXPECT_EQ(permittedViewCamera(*scene.objectManager, 1, other, true), other);
  EXPECT_EQ(permittedViewCamera(*scene.objectManager, 1, unowned, true), unowned);

  EXPECT_TRUE(permittedViewCamera(*scene.objectManager, 1, other, false).empty());
  EXPECT_TRUE(permittedViewCamera(*scene.objectManager, 1, unowned, false).empty());
  EXPECT_TRUE(permittedViewCamera(*scene.objectManager, 5, own, false).empty());
}

TEST(PermittedViewCamera, EmptyOrMalformedRequestsAreEmptyEvenForAnEditor)
{
  const auto scene = fixtures::makeScene();
  const auto mine = fixtures::addObject(scene, "Mine");
  addController(mine, 1);
  addCamera(mine);

  EXPECT_FALSE(permittedViewCamera(*scene.objectManager, 1, uuids::to_string(mine->getUUID()), false).empty());
  for (const bool editor : { false, true })
  {
    EXPECT_TRUE(permittedViewCamera(*scene.objectManager, 1, "", editor).empty());
    EXPECT_TRUE(permittedViewCamera(*scene.objectManager, 1, "not-a-uuid", editor).empty());
  }
}
