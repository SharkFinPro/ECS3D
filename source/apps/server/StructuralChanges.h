#ifndef STRUCTURALCHANGES_H
#define STRUCTURALCHANGES_H

#include <Protocol.h>
#include <Replication.h>
#include <SceneObjectDiff.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <memory>
#include <utility>
#include <uuid.h>
#include <vector>

// What a tick's scripts did to the scene, replicated after the tick. The take callables stand in for
// BindingContext's buffers (scripting can't reach the net layer) so the order they are read in is testable;
// objectManager is null while no scene is loaded.
template <typename TakeComponentEdits, typename TakeSpawned, typename TakeDestroyed, typename TakeChanged,
          typename Broadcast, typename NotifyObjectEvents>
void replicateStructuralChanges(ObjectManager* objectManager, TakeComponentEdits&& takeComponentEdits,
                                TakeSpawned&& takeSpawned, TakeDestroyed&& takeDestroyed, TakeChanged&& takeChanged,
                                Broadcast&& broadcast, NotifyObjectEvents&& notifyObjectEvents)
{
  // A script's component edit (e.g. swapping a model/texture) isn't covered by the per-tick state delta,
  // which only carries Transform, so it replicates like an editor edit.
  for (const auto& [objectUUID, component] : takeComponentEdits())
  {
    broadcast(replication::buildComponentEdit(objectUUID, component));
  }

  // Spawns go out before destroys, both before the flush. A spawned object is still pending here, so its
  // packed blob carries its current transform and components.
  const auto spawned = takeSpawned();
  for (const auto& object : spawned)
  {
    broadcast(replication::buildObjectSpawned(*object));
  }

  const auto destroyed = takeDestroyed();
  for (const auto& uuid : destroyed)
  {
    broadcast(replication::buildObjectDestroyed(uuid));
  }

  // Additions flush before deletions, so an object spawned then destroyed in the same pass is a real member
  // of the list by the time the delete pass looks for it.
  std::vector<uuids::uuid> spawnedUUIDs;
  if (objectManager)
  {
    objectManager->flushPendingAdditions();
    objectManager->deleteObjectsMarkedForDeletion();

    // An object spawned and destroyed in the same pass is gone by now and is not reported as spawned.
    for (const auto& uuid : sceneObjectDiff::flattenSubtrees(spawned))
    {
      if (objectManager->getObjectByUUID(uuid))
      {
        spawnedUUIDs.push_back(uuid);
      }
    }
  }

  // Handlers run here can spawn or destroy too; those land in the buffers for the next call rather than
  // recursing.
  notifyObjectEvents(spawnedUUIDs, destroyed);

  // A script's addComponent/removeComponent already applied to each object listed here, but the per-tick
  // delta doesn't carry it, so each changed object's packed state goes out as its own message. Taken after
  // the handlers so their changes are included, and read after the deletion pass so an object that left the
  // scene this tick (already covered by an objectDestroyed) is skipped.
  const auto changedUUIDs = takeChanged();
  if (objectManager)
  {
    for (const auto& objectUUID : changedUUIDs)
    {
      if (const auto object = objectManager->getObjectByUUID(objectUUID))
      {
        broadcast(replication::buildObjectComponentsChanged(*object));
      }
    }
  }
}

#endif //STRUCTURALCHANGES_H
