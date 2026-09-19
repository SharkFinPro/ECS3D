#include "ServerApp.h"
#include <Replication.h>
#include <assets/AssetRegistry.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <ScriptSystem.h>
#include <NetServer.h>
#include <Log.h>
#include <nlohmann/json.hpp>
#include <string>

namespace {
  const char* describe(const replication::SceneEditResult result)
  {
    switch (result)
    {
      case replication::SceneEditResult::malformedEdit: return "the edit is missing a field it needs";
      case replication::SceneEditResult::unknownObject: return "no such object";
      case replication::SceneEditResult::unknownComponent: return "no such component";
      case replication::SceneEditResult::unknownAsset: return "no such prefab";
      case replication::SceneEditResult::rejected: return "it would change nothing, or make a cycle";
      case replication::SceneEditResult::failed: return "it threw part way through";
      case replication::SceneEditResult::applied: return "it was applied";
    }

    return "it was applied";
  }
}

void ServerApp::handleEditComponent(const net::Message& message) const
{
  // An editor changed a component: apply it to the authoritative scene, then re-broadcast so every
  // other view (and the editing client, idempotently) converges.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    // Only an edit the authority actually applied gets rebroadcast. Rebroadcasting one it could not
    // apply would push every client away from the authoritative state, and a payload it could not parse
    // would fail identically on every one of them. Nothing below this point can rely on the message
    // being well formed either, since it re-reads it.
    const auto result = replication::applyComponentEdit(*scene->getObjectManager(), message);

    if (result != replication::ComponentEditResult::applied)
    {
      Log::error(LogCategory::server, "Discarded a component edit of " + std::to_string(message.size()) +
                          " bytes: " + std::string(replication::describe(result)) + ".");

      // A half-written component has no delta stream to correct it for most types, so the only way back
      // to agreement is a fresh snapshot.
      if (result == replication::ComponentEditResult::partiallyApplied)
      {
        broadcastSnapshot();
      }

      return;
    }

    m_netServer->broadcast(message);

    // If the edit targets a Script, push the new field values into the live C# instance so the
    // running behavior reflects the change immediately (applyComponentEdit only updates the data
    // layer; the C# instance is owned by ScriptSystem and needs an explicit write). The packed layout
    // mirrors Script::pack: [object uuid][type][className][fields].
    net::MessageReader reader(message);
    const auto objectUUID = uuids::uuid::from_string(reader.readString());

    if (objectUUID.has_value() && reader.read<ComponentType>() == ComponentType::script)
    {
      const auto className = reader.readString();
      const auto fields = nlohmann::json::parse(reader.readString(), nullptr, false);

      if (!fields.is_discarded())
      {
        m_scriptSystem->applyScriptFieldEdit(objectUUID.value(), className, fields);
      }
    }
  }
}

void ServerApp::handleSceneEdit(const net::Message& message) const
{
  // An editor changed the scene graph (add/remove object or component, or instantiate a prefab): apply
  // it, then re-snapshot so every view rebuilds (structural changes aren't replicated per-op). The
  // registry is passed so the prefab op can resolve its asset uuid to the body on disk.
  const auto scene = m_sceneManager->getCurrentScene();
  if (!scene)
  {
    Log::error(LogCategory::server, "Discarded a scene edit: no scene is loaded.");
    return;
  }

  const std::string payload(message.bytes().begin(), message.bytes().end());

  const auto json = nlohmann::json::parse(payload, nullptr, false);
  if (json.is_discarded())
  {
    Log::error(LogCategory::server, "Discarded a scene edit of " + std::to_string(message.size()) +
                        " bytes: it is not JSON.");
    return;
  }

  const auto result = replication::applySceneEdit(*scene->getObjectManager(), json, m_assetRegistry.get());

  if (result != replication::SceneEditResult::applied)
  {
    // Read defensively. A payload of [] or {"op": 5} is valid JSON, so it reaches here - and value()
    // throws on a json that is not an object, or on a key that will not convert. Throwing out of the
    // log line would put this back in the run loop's generic catch, which is the outcome this handler
    // exists to replace.
    const auto op = json.is_object() && json.contains("op") && json.at("op").is_string()
      ? json.at("op").get<std::string>()
      : std::string("no op");

    Log::error(LogCategory::server, "Discarded a scene edit (" + op + "): " + describe(result) + ".");

    // A refusal the sender could have predicted needs no snapshot: rejected means the authority and the
    // sender agree about the scene and the op simply changes nothing, and a malformed edit is a payload
    // problem that resending the scene would not fix - and is the one a client can send on demand. The
    // exception is a creating op refused for the uuid it chose: the sender gets neither the object nor
    // an error, so it has to notice the missing object itself rather than being resynced here.
    //
    // Everything else means the sender's view disagrees with the authority: an object or component it
    // believes exists and does not, a prefab it cannot resolve, an edit that threw part way through.
    // Those are exactly the cases a snapshot repairs, and there is no client-initiated resync to fall
    // back on - a structural edit was the only thing that rebuilt a drifted view.
    if (result != replication::SceneEditResult::rejected &&
        result != replication::SceneEditResult::malformedEdit)
    {
      broadcastSnapshot();
    }

    return;
  }

  broadcastSnapshot();
}

void ServerApp::handleAddAsset(const net::Message& message) const
{
  // An editor imported/created an asset: register it in the authoritative registry and re-snapshot.
  nlohmann::json asset;
  try
  {
    asset = replication::unpackAddAsset(message);
  }
  catch (const std::exception&)
  {
    return;
  }

  replication::applyAddAsset(*m_assetRegistry, *m_sceneManager, m_componentRegistry, asset);

  Log::info(LogCategory::server, "Registered asset (" + asset.value("assetType", std::string{}) + ").");

  broadcastSnapshot();
}

void ServerApp::handleRenameAsset(const net::Message& message) const
{
  // An editor renamed an asset (display-name override only): apply it authoritatively and re-snapshot.
  nlohmann::json op;
  try
  {
    op = replication::unpackRenameAsset(message);
  }
  catch (const std::exception&)
  {
    return;
  }

  replication::applyRenameAsset(*m_assetRegistry, op);

  Log::info(LogCategory::server, "Renamed asset.");

  broadcastSnapshot();
}

void ServerApp::handleRemoveAsset(const net::Message& message) const
{
  // An editor deleted an asset: drop the record and re-snapshot. References dangle by design (lookups
  // null-tolerate a missing uuid).
  nlohmann::json op;
  try
  {
    op = replication::unpackRemoveAsset(message);
  }
  catch (const std::exception&)
  {
    return;
  }

  replication::applyRemoveAsset(*m_assetRegistry, op);

  Log::info(LogCategory::server, "Removed asset.");

  broadcastSnapshot();
}
