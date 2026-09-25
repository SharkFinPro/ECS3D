#include "ObjectManager.h"
#include "Object.h"
#include "WorldPlacement.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>
#include <algorithm>
#include <array>
#include <functional>
#include <stdexcept>

ObjectManager::ObjectManager(std::shared_ptr<ComponentRegistry> componentRegistry)
  : m_componentRegistry(std::move(componentRegistry)),
    m_rng([] {
      std::random_device rd;
      auto seed_data = std::array<int, std::mt19937::state_size>{};
      std::ranges::generate(seed_data, std::ref(rd));
      std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
      return std::mt19937(seq);
    }()),
    m_uuidGenerator(m_rng)
{}

std::shared_ptr<ComponentRegistry> ObjectManager::getComponentRegistry() const
{
  return m_componentRegistry;
}

uuids::uuid ObjectManager::createUUID()
{
  return m_uuidGenerator();
}

void ObjectManager::addObject(const std::shared_ptr<Object>& object)
{
  object->setManager(this);

  // Colliders aren't registered here: CollisionSystem discovers them by scanning the objects each tick.

  if (m_scriptPassDepth > 0)
  {
    // A script pass is ranging over m_allObjects right now, running user code inside the loop
    // (ScriptSystem::fixedUpdate/variableUpdate); appending straight to m_allObjects/m_objects could
    // reallocate the vector out from under that range-for. Parenting and starting the object touch
    // neither vector, so they still happen immediately - the spawning script can position or query the
    // object it just created in the same call. Only the flat-list membership waits for
    // flushPendingAdditions; getObjectByUUID checks the pending list too, so lookups keep working meanwhile.
    if (object->getParent() != nullptr)
    {
      object->getParent()->addChild(object);
    }

    if (m_started)
    {
      object->start();
    }

    m_pendingAdditions.push_back(object);
    return;
  }

  m_allObjects.push_back(object);

  if (object->getParent() == nullptr)
  {
    m_objects.push_back(object);
  }
  else
  {
    object->getParent()->addChild(object);
  }

  // Registered while the scene is running: without this the object's components stay backed by their
  // authored values, so anything the run writes to them is saved into the scene as if it had been
  // authored. This is the object-level counterpart of Object::addComponent's own start.
  if (m_started)
  {
    object->start();
  }
}

ObjectManager::ScriptPassGuard::ScriptPassGuard(ObjectManager& manager)
  : m_manager(manager)
{
  ++m_manager.m_scriptPassDepth;
}

ObjectManager::ScriptPassGuard::~ScriptPassGuard()
{
  --m_manager.m_scriptPassDepth;
}

void ObjectManager::flushPendingAdditions()
{
  if (m_pendingAdditions.empty())
  {
    return;
  }

  // Taken out first: deleteObjectsMarkedForDeletion runs right after this call (see
  // ServerApp::broadcastStructuralChanges), and an object a script spawned then destroyed in the same
  // pass has to be a real member of m_allObjects/m_objects before that pass can find and remove it.
  const auto pending = std::move(m_pendingAdditions);
  m_pendingAdditions.clear();

  for (const auto& object : pending)
  {
    m_allObjects.push_back(object);

    if (object->getParent() == nullptr)
    {
      m_objects.push_back(object);
    }
  }
}

void ObjectManager::addObjectToRoot(const std::shared_ptr<Object>& object)
{
  m_objects.push_back(object);
}

void ObjectManager::addObjectToRoot(const std::shared_ptr<Object>& object, const std::size_t index)
{
  const std::size_t clampedIndex = std::min(index, m_objects.size());
  m_objects.insert(m_objects.begin() + static_cast<std::ptrdiff_t>(clampedIndex), object);
}

void ObjectManager::removeObjectFromRoot(const std::shared_ptr<Object>& object)
{
  std::erase(m_objects, object);
}

namespace {
  // Number of ancestors above object (root = 0), by a plain walk up getParent() - never more than
  // maxObjectDepth steps for any object that arrived through a depth-checked path.
  std::size_t ancestorDepth(const std::shared_ptr<Object>& object)
  {
    std::size_t depth = 0;
    for (auto current = object->getParent(); current; current = current->getParent())
    {
      ++depth;
    }

    return depth;
  }

  // object's own position among its current siblings (root list when it has no parent), read before it
  // is removed from that list - deleteObjectsMarkedForDeletion uses this to promote its children into the
  // slot it vacated instead of appending them at the end.
  std::size_t siblingIndexOf(const ObjectManager& manager, const std::shared_ptr<Object>& object)
  {
    const auto parent = object->getParent();
    const auto& siblings = parent ? parent->getChildren() : manager.getObjects();

    for (std::size_t index = 0; index < siblings.size(); ++index)
    {
      if (siblings[index] == object)
      {
        return index;
      }
    }

    return siblings.size();
  }
}

void ObjectManager::reassignUUIDs(nlohmann::json& objectData, const std::size_t depth)
{
  if (depth > maxObjectDepth)
  {
    throw std::runtime_error("Object nesting exceeds maximum depth");
  }

  objectData["uuid"] = uuids::to_string(createUUID());

  if (objectData.contains("children"))
  {
    for (auto& child : objectData.at("children"))
    {
      reassignUUIDs(child, depth + 1);
    }
  }
}

std::shared_ptr<Object> ObjectManager::instantiateUnder(const nlohmann::json& objectData,
                                                       const std::shared_ptr<Object>& parent,
                                                       const uuids::uuid* rootUUID)
{
  // Work on a copy: reassignUUIDs rewrites the blob, and a prefab body is reused across instantiations.
  auto data = objectData;

  // A body is only as deep as it claims to be in isolation - but landing it under a live parent adds
  // that parent's own depth on top, and this is reachable under any object (duplicateObject passes the
  // source's own parent; an instantiatePrefab that can target an arbitrary object would too), so the
  // combined tree is what has to fit under maxObjectDepth, not just the body by itself.
  const std::size_t baseDepth = parent ? ancestorDepth(parent) + 1 : 0;

  // Give the new object (and every descendant) fresh uuids - reusing the originals would collide in the
  // uuid-keyed replication/picking.
  reassignUUIDs(data, baseDepth);

  // Written over the freshly generated one rather than skipping the root in reassignUUIDs, so the depth
  // walk that assigns the descendants is the same one either way.
  if (rootUUID)
  {
    data["uuid"] = uuids::to_string(*rootUUID);
  }

  const auto newObject = std::make_shared<Object>(data, this);
  newObject->setParent(parent);

  addObject(newObject);

  // Object's ctor loads only its own components/scripts; children are separate objects, so build them.
  // Each child registers itself before it is loaded, so a body whose child names a component this build
  // does not know would strand a half-built subtree - and callers here are expected to catch and carry
  // on, which is precisely what leaves the wreckage in the scene. serialize() always writes "children" as
  // an array, empty for a leaf, so an emptiness check (not just contains()) is what tells a leaf body from
  // one with something to load - otherwise a leaf landing exactly at maxObjectDepth would be rejected for
  // children it does not have.
  if (const auto childrenIt = data.find("children"); childrenIt != data.end() && !childrenIt->empty())
  {
    try
    {
      newObject->loadChildren(*childrenIt, baseDepth + 1);
    }
    catch (...)
    {
      discardSubtree(newObject);
      throw;
    }
  }

  return newObject;
}

std::shared_ptr<Object> ObjectManager::instantiate(const nlohmann::json& objectData)
{
  return instantiateUnder(objectData, nullptr);
}

void ObjectManager::duplicateObject(const std::shared_ptr<Object>& object, const uuids::uuid* rootUUID)
{
  auto objectData = object->serialize();
  objectData["name"] = std::string(objectData.at("name")) + " - Copy";

  // A duplicate sits beside its original; a prefab instance (instantiate) lands at the scene root.
  instantiateUnder(objectData, object->getParent(), rootUUID);
}

void ObjectManager::start()
{
  m_started = true;

  for (const auto& object : m_allObjects)
  {
    object->start();
  }
}

void ObjectManager::stop()
{
  m_started = false;

  for (const auto& object : m_allObjects)
  {
    object->stop();
  }
}

nlohmann::json ObjectManager::serialize() const
{
  nlohmann::json data = {
    { "objects", nlohmann::json::array() }
  };

  for (const auto& object : m_objects)
  {
    data["objects"].push_back(object->serialize());
  }

  return data;
}

void ObjectManager::pack(net::Message& message) const
{
  // Mirrors serialize(): only the root objects are written, each packing its own subtree recursively.
  message.write(static_cast<uint32_t>(m_objects.size()));

  for (const auto& object : m_objects)
  {
    object->pack(message);
  }
}

void ObjectManager::restoreFromJSON(const nlohmann::json& objectsData)
{
  // Mirrors unpack(): replace, not append, and no fresh uuids - the run's spawn/destroy/reparent has
  // already happened, so this puts the tree back exactly as it was captured before the run started.
  m_objects.clear();
  m_allObjects.clear();
  m_objectsToRemove.clear();
  m_pendingAdditions.clear();

  for (const auto& objectData : objectsData)
  {
    auto object = std::make_shared<Object>(objectData, this);
    addObject(object);

    if (objectData.contains("children"))
    {
      object->loadChildren(objectData.at("children"));
    }
  }
}

void ObjectManager::unpack(net::MessageReader& messageReader)
{
  // Replace, not append: every caller today unpacks into a manager it just constructed (see
  // ProjectPacker::unpack/SceneAsset::unpack, which parse into fresh instances and swap on success), so
  // clearing here can't undo a live scene - but a manager that already holds objects must not keep them
  // once a new snapshot is read into it.
  m_objects.clear();
  m_allObjects.clear();
  m_objectsToRemove.clear();
  m_pendingAdditions.clear();

  const uint32_t objectCount = messageReader.read<uint32_t>();

  for (uint32_t i = 0; i < objectCount; ++i)
  {
    auto object = std::make_shared<Object>();
    addObject(object);

    object->unpack(messageReader);
  }
}

bool ObjectManager::removeObject(const std::shared_ptr<Object>& object)
{
  if (std::ranges::find(m_objectsToRemove, object) != m_objectsToRemove.end())
  {
    return false;
  }

  m_objectsToRemove.push_back(object);
  return true;
}

bool ObjectManager::isMarkedForDeletion(const std::shared_ptr<Object>& object) const
{
  return std::ranges::find(m_objectsToRemove, object) != m_objectsToRemove.end();
}

void ObjectManager::deleteObjectsMarkedForDeletion()
{
  if (m_objectsToRemove.empty())
  {
    return;
  }

  for (const auto& object : m_objectsToRemove)
  {
    const auto parent = object->getParent();

    // Read before the erase below shifts anything: this is the slot object occupied among its own
    // siblings, and its children are promoted starting there so they keep the relative order they had
    // under it instead of landing at the end of the new parent's list.
    const std::size_t insertIndex = siblingIndexOf(*this, object);

    if (parent)
    {
      parent->removeChild(object);
    }
    else
    {
      removeObjectFromRoot(object);
    }

    const auto children = object->getChildren();
    for (std::size_t childOffset = 0; childOffset < children.size(); ++childOffset)
    {
      const auto& child = children[childOffset];

      // Captured while child's parent chain still runs through the deleted object, so its world
      // placement includes that object's own transform - the delete confirmation promises children are
      // kept in place, not left at the new parent's origin.
      const auto placement = captureWorldPlacement(child);

      object->removeChild(child);
      child->setParent(parent);

      const std::size_t childIndex = insertIndex + childOffset;
      if (parent)
      {
        parent->addChild(child, childIndex);
      }
      else
      {
        addObjectToRoot(child, childIndex);
      }

      if (placement)
      {
        restoreWorldPlacement(child, parent, *placement);
      }
    }

    // (No CollisionSystem deregistration needed: it rescans the live objects each tick, so a removed
    // object naturally drops out.)

    std::erase(m_allObjects, object);
  }

  m_objectsToRemove.clear();
}

std::shared_ptr<Object> ObjectManager::restoreSubtree(const nlohmann::json& body,
                                                       const std::shared_ptr<Object>& parent,
                                                       const std::size_t index)
{
  // uuids are preserved, unlike instantiateUnder (prefab drop) and duplicateObject: the undo history
  // already names this subtree's objects by these uuids (EditCommand::RemoveObjectData), so minting
  // fresh ones the way a prefab instantiation does would break every entry that points at them.
  const auto root = std::make_shared<Object>(body, this);
  root->setParent(parent);

  const std::size_t baseDepth = parent ? ancestorDepth(parent) + 1 : 0;

  m_allObjects.push_back(root);

  if (parent)
  {
    parent->addChild(root, index);
  }
  else
  {
    addObjectToRoot(root, index);
  }

  if (m_started)
  {
    root->start();
  }

  // Same shape as instantiateUnder: a body whose child names a component this build does not know
  // would otherwise strand a half-built subtree in the scene.
  try
  {
    if (const auto childrenIt = body.find("children"); childrenIt != body.end() && !childrenIt->empty())
    {
      root->loadChildren(*childrenIt, baseDepth + 1);
    }
  }
  catch (...)
  {
    discardSubtree(root);
    throw;
  }

  return root;
}

void ObjectManager::removeSubtree(const std::shared_ptr<Object>& object)
{
  // discardSubtree already does exactly this for a live subtree - detach from its parent or the root
  // list and erase it and every descendant from m_allObjects - so it is reused rather than walking the
  // tree a second time.
  discardSubtree(object);
}

void ObjectManager::discardSubtree(std::shared_ptr<Object> root)
{
  if (!root)
  {
    return;
  }

  // The root's parent stays in the scene, so its reference has to go too - otherwise the subtree is gone
  // from the manager but still hanging off a live object.
  if (const auto parent = root->getParent())
  {
    parent->removeChild(root);
  }

  eraseSubtree(root);
}

void ObjectManager::eraseSubtree(const std::shared_ptr<Object>& object)
{
  for (const auto& child : object->getChildren())
  {
    eraseSubtree(child);
  }

  std::erase(m_allObjects, object);
  std::erase(m_objects, object);

  // A queued removal would otherwise outlive the discard and be reparented back into the scene by the
  // next deletion pass.
  std::erase(m_objectsToRemove, object);

  // A subtree discarded mid script pass (instantiateUnder/restoreSubtree unwinding a bad body) may still
  // be sitting in the pending-additions queue rather than m_allObjects/m_objects yet - without this,
  // flushPendingAdditions would resurrect the wreckage this call is meant to drop.
  std::erase(m_pendingAdditions, object);
}

std::shared_ptr<Object> ObjectManager::getObjectByUUID(const uuids::uuid uuid) const
{
  for (const auto& object : m_allObjects)
  {
    if (object->getUUID() == uuid)
    {
      return object;
    }
  }

  // A script that just spawned an object (World.spawn/spawnPrefab) may look it up or act on it again in
  // the same pass, before flushPendingAdditions makes it a real m_allObjects member - see addObject.
  for (const auto& object : m_pendingAdditions)
  {
    if (object->getUUID() == uuid)
    {
      return object;
    }
  }

  return nullptr;
}

const std::vector<std::shared_ptr<Object>>& ObjectManager::getObjects() const
{
  return m_objects;
}

const std::vector<std::shared_ptr<Object>>& ObjectManager::getAllObjects() const
{
  return m_allObjects;
}

const std::vector<std::shared_ptr<Object>>& ObjectManager::getPendingAdditions() const
{
  return m_pendingAdditions;
}

std::unique_ptr<ObjectManager> makeScratchCopy(const ObjectManager& source)
{
  auto copy = std::make_unique<ObjectManager>(source.getComponentRegistry());
  copy->restoreFromJSON(source.serialize().at("objects"));
  return copy;
}
