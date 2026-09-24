#include "PlayerControllerBindings.h"
#include "BindingContext.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/PlayerController.h>
#include <memory>
#include <string>

namespace {
  // Also hands back the parsed object uuid, so a setter that needs to record a replicated edit doesn't
  // have to re-parse the string it just resolved.
  std::shared_ptr<PlayerController> find(const char* uuid, uuids::uuid* outObjectUUID = nullptr)
  {
    const auto objectManager = BindingContext::getObjectManager();
    if (!objectManager || !uuid)
    {
      return nullptr;
    }

    const auto parsed = uuids::uuid::from_string(std::string(uuid));
    if (!parsed.has_value())
    {
      return nullptr;
    }

    const auto object = objectManager->getObjectByUUID(parsed.value());
    if (!object)
    {
      return nullptr;
    }

    const auto playerController = object->getComponent<PlayerController>(ComponentType::playerController);
    if (playerController && outObjectUUID)
    {
      *outObjectUUID = parsed.value();
    }

    return playerController;
  }
}

PlayerControllerBindings PlayerControllerBindingsProvider::getBindings()
{
  return PlayerControllerBindings {
    .has = &bindHas,
    .getPlayerSlot = &bindGetPlayerSlot,
    .setPlayerSlot = &bindSetPlayerSlot
  };
}

bool PlayerControllerBindingsProvider::bindHas(const char* uuid)
{
  return find(uuid) != nullptr;
}

int32_t PlayerControllerBindingsProvider::bindGetPlayerSlot(const char* uuid)
{
  const auto playerController = find(uuid);
  if (!playerController)
  {
    return 0;
  }

  return playerController->getPlayerSlot();
}

void PlayerControllerBindingsProvider::bindSetPlayerSlot(const char* uuid, const int32_t playerSlot)
{
  uuids::uuid objectUUID;
  const auto playerController = find(uuid, &objectUUID);
  if (!playerController)
  {
    return;
  }

  const auto before = playerController->getPlayerSlot();
  playerController->setPlayerSlot(playerSlot);
  const auto after = playerController->getPlayerSlot();

  if (before != after)
  {
    // Not covered by the per-tick state delta (Transform only), so replicate it like the editor's own
    // component edits: buffer it here (scripting can't reach the net layer) for the app to broadcast.
    BindingContext::recordComponentEdit(objectUUID, playerController);
  }
}
