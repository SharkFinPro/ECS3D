#include "ScriptSystem.h"
#include "ScriptFieldEdit.h"
#include "bindings/BindingContext.h"
#include <Log.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/Script.h>
#include <scenes/SceneAsset.h>
#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>

ScriptSystem::ScriptSystem(RuntimeFactory makeRuntime, std::filesystem::path userScriptsDir)
  : m_makeRuntime(std::move(makeRuntime)),
    m_userScriptsDir(std::move(userScriptsDir))
{}

ScriptSystem::~ScriptSystem() = default;

void ScriptSystem::warmUp()
{
  ensureEngine();
}

void ScriptSystem::ensureEngine()
{
  if (m_engine)
  {
    return;
  }

  // Built once on first use (compiling the user scripts is heavy); a throw leaves m_engine null so the
  // next call retries.
  m_engine = m_makeRuntime();

  m_scriptsSnapshot = takeSnapshot();
}

void ScriptSystem::start(ObjectManager& objectManager)
{
  ensureEngine();

  BindingContext::setObjectManager(&objectManager);

  startObjects(objectManager);
}

void ScriptSystem::start(SceneAsset& scene)
{
  ensureEngine();

  const auto objectManager = scene.getObjectManager();
  BindingContext::setObjectManager(objectManager.get());

  // Scene scripts start before every object script. Iterates a copy of the list, since a script's start()
  // could reach code that edits it.
  for (const auto& script : std::vector(scene.getScripts()))
  {
    attachSceneScript(scene, script);
    if (isSceneScriptAttached(scene.getUUID(), script->getClassName()))
    {
      startSceneScriptIfNeeded(scene.getUUID(), script->getClassName());
    }
  }

  startObjects(*objectManager);

  dispatchSceneEvent(scene, SceneEvent::loaded);
}

void ScriptSystem::startObjects(ObjectManager& objectManager)
{
  for (const auto& object : objectManager.getAllObjects())
  {
    for (const auto& scriptComponent : object->getScripts())
    {
      const auto script = std::dynamic_pointer_cast<Script>(scriptComponent);
      if (!script)
      {
        continue;
      }

      attach(*object, script);
      if (isAttached(object->getUUID(), script->getClassName()))
      {
        startIfNeeded(object->getUUID(), script->getClassName());
      }
    }
  }
}

void ScriptSystem::stop(ObjectManager& objectManager)
{
  if (!m_engine)
  {
    return;
  }

  BindingContext::setObjectManager(&objectManager);

  stopObjects(objectManager);
}

void ScriptSystem::stop(SceneAsset& scene)
{
  if (!m_engine)
  {
    return;
  }

  const auto objectManager = scene.getObjectManager();
  BindingContext::setObjectManager(objectManager.get());

  // Before any stop(), so the handler still sees a fully running scene.
  dispatchSceneEvent(scene, SceneEvent::unloading);

  // Object scripts first, then the scene's, so a scene script outlives everything it coordinates.
  stopObjects(*objectManager);

  for (const auto& script : std::vector(scene.getScripts()))
  {
    const auto& className = script->getClassName();
    if (!isSceneScriptAttached(scene.getUUID(), className))
    {
      continue;
    }

    if (m_startedSceneScripts.contains(cacheKey(scene.getUUID(), className)))
    {
      m_engine->stop(uuids::to_string(scene.getUUID()).c_str(), className.c_str());
    }

    detachSceneScript(scene.getUUID(), className);
  }

  detachSceneScriptOrphans(scene);
}

void ScriptSystem::stopObjects(ObjectManager& objectManager)
{
  for (const auto& object : objectManager.getAllObjects())
  {
    for (const auto& scriptComponent : object->getScripts())
    {
      const auto script = std::dynamic_pointer_cast<Script>(scriptComponent);
      if (!script)
      {
        continue;
      }

      const auto uuid = object->getUUID();
      const auto className = script->getClassName();

      if (!isAttached(uuid, className))
      {
        continue;
      }

      m_engine->stop(uuids::to_string(uuid).c_str(), className.c_str());
      detach(uuid, className);
    }
  }

  // Anything still attached belongs to a Script component or an object that left the scene during the
  // run; the loop above cannot reach it, so the sweep is what leaves m_attached empty.
  detachOrphans(objectManager);
}

void ScriptSystem::fixedUpdate(ObjectManager& objectManager, const float dt)
{
  ensureEngine();

  // Point the C-ABI bindings at the scene so script callbacks (applyForce, move, ...) can resolve a
  // uuid to its object while they run.
  BindingContext::setObjectManager(&objectManager);

  checkForScriptChanges([&] { syncFieldsToData(objectManager); }, dt);

  // Before the attach loop, so an instance orphaned since the last sweep is gone by the time attach
  // looks at the component that replaced it.
  detachOrphans(objectManager);

  // World.spawnObject/spawnPrefab, called from a script running inside this loop, must not append
  // straight to objectManager's live m_allObjects/m_objects: this range-for holds iterators into that
  // vector, and a reallocation mid-loop is undefined behavior. The guard makes addObject defer the new
  // object instead - it is still immediately usable (getObjectByUUID finds it, its transform can be set),
  // it just doesn't join getAllObjects() until flushPendingAdditions runs after the tick.
  const ObjectManager::ScriptPassGuard scriptPassGuard(objectManager);

  fixedUpdateObjects(objectManager, dt);
}

void ScriptSystem::fixedUpdate(SceneAsset& scene, const float dt)
{
  ensureEngine();

  const auto objectManager = scene.getObjectManager();
  BindingContext::setObjectManager(objectManager.get());

  checkForScriptChanges([&] { syncFieldsToData(scene); }, dt);

  detachOrphans(*objectManager);
  detachSceneScriptOrphans(scene);

  // Held across the scene scripts too: one of them can spawn objects through the World bindings.
  const ObjectManager::ScriptPassGuard scriptPassGuard(*objectManager);

  for (const auto& script : std::vector(scene.getScripts()))
  {
    const auto& className = script->getClassName();
    if (!isSceneScriptAttached(scene.getUUID(), className))
    {
      attachSceneScript(scene, script);
    }

    if (!isSceneScriptAttached(scene.getUUID(), className))
    {
      continue;
    }

    startSceneScriptIfNeeded(scene.getUUID(), className);

    m_engine->fixedUpdate(uuids::to_string(scene.getUUID()).c_str(), className.c_str(), dt);
  }

  fixedUpdateObjects(*objectManager, dt);
}

void ScriptSystem::fixedUpdateObjects(ObjectManager& objectManager, const float dt)
{
  for (const auto& object : objectManager.getAllObjects())
  {
    for (const auto& scriptComponent : object->getScripts())
    {
      const auto script = std::dynamic_pointer_cast<Script>(scriptComponent);
      if (!script)
      {
        continue;
      }

      // Lazily (re)create the instance if needed - restores instances after a hot reload, and covers a
      // script added to a running scene (new component, or an object spawned by World.spawnObject/
      // spawnPrefab).
      if (!isAttached(object->getUUID(), script->getClassName()))
      {
        attach(*object, script);
      }

      if (!isAttached(object->getUUID(), script->getClassName()))
      {
        continue;
      }

      // The scene is running here (fixedUpdate is only called while it is), so any instance that isn't
      // started yet - whether just attached above or attached earlier by attachAll while the snapshot
      // was built - gets its one start() before this first tick.
      startIfNeeded(object->getUUID(), script->getClassName());

      m_engine->fixedUpdate(uuids::to_string(object->getUUID()).c_str(), script->getClassName().c_str(), dt);
    }
  }
}

void ScriptSystem::variableUpdate(ObjectManager& objectManager)
{
  if (!m_engine)
  {
    return;
  }

  BindingContext::setObjectManager(&objectManager);

  // Same reasoning as fixedUpdate's guard: a script below can spawn an object through World bindings while
  // this range-for holds iterators into objectManager's live list, so additions must defer until the pass
  // ends.
  const ObjectManager::ScriptPassGuard scriptPassGuard(objectManager);

  variableUpdateObjects(objectManager);
}

void ScriptSystem::variableUpdate(SceneAsset& scene)
{
  if (!m_engine)
  {
    return;
  }

  const auto objectManager = scene.getObjectManager();
  BindingContext::setObjectManager(objectManager.get());

  const ObjectManager::ScriptPassGuard scriptPassGuard(*objectManager);

  for (const auto& script : std::vector(scene.getScripts()))
  {
    const auto& className = script->getClassName();

    // Like object scripts, only an instance fixedUpdate or start already attached runs here.
    if (!isSceneScriptAttached(scene.getUUID(), className))
    {
      continue;
    }

    startSceneScriptIfNeeded(scene.getUUID(), className);

    m_engine->variableUpdate(uuids::to_string(scene.getUUID()).c_str(), className.c_str());
  }

  variableUpdateObjects(*objectManager);
}

void ScriptSystem::variableUpdateObjects(ObjectManager& objectManager)
{
  for (const auto& object : objectManager.getAllObjects())
  {
    for (const auto& scriptComponent : object->getScripts())
    {
      const auto script = std::dynamic_pointer_cast<Script>(scriptComponent);
      if (!script)
      {
        continue;
      }

      // Only run instances that have already been attached (fixedUpdate owns lazy attachment); a script
      // added this same tick waits for fixedUpdate to attach it and runs variableUpdate from next tick on.
      if (!isAttached(object->getUUID(), script->getClassName()))
      {
        continue;
      }

      // attachAll (run from broadcastSnapshot, running or not) can attach an instance before the scene's
      // tick loop ever reaches it; start it here, before its first variableUpdate, since the scene is
      // running whenever this runs.
      startIfNeeded(object->getUUID(), script->getClassName());

      m_engine->variableUpdate(uuids::to_string(object->getUUID()).c_str(), script->getClassName().c_str());
    }
  }
}

void ScriptSystem::dispatchCollisionEvent(ObjectManager& objectManager,
                                          const uuids::uuid& objectA,
                                          const uuids::uuid& objectB,
                                          const CollisionEvent event) const
{
  if (!m_engine || (!m_attachedIndex.hasAnyAttached(objectA) && !m_attachedIndex.hasAnyAttached(objectB)))
  {
    return;
  }

  // Point the bindings at the scene so a script's collision handler can resolve uuids (move, applyForce,
  // World queries) while it runs, just like fixedUpdate does.
  BindingContext::setObjectManager(&objectManager);

  dispatchCollisionTo(objectManager, objectA, objectB, event);
  dispatchCollisionTo(objectManager, objectB, objectA, event);
}

void ScriptSystem::dispatchCollisionTo(ObjectManager& objectManager,
                                       const uuids::uuid& target,
                                       const uuids::uuid& other,
                                       const CollisionEvent event) const
{
  if (!m_attachedIndex.hasAnyAttached(target))
  {
    return;
  }

  const auto object = objectManager.getObjectByUUID(target);
  if (!object)
  {
    // The object was destroyed (e.g. this is the exit event for a contact that ended because it was
    // removed). Nothing to notify on this side.
    return;
  }

  const auto targetStr = uuids::to_string(target);
  const auto otherStr = uuids::to_string(other);
  const auto eventCode = static_cast<int>(event);

  for (const auto& scriptComponent : object->getScripts())
  {
    const auto script = std::dynamic_pointer_cast<Script>(scriptComponent);
    if (!script)
    {
      continue;
    }

    // Only notify instances that have been attached + started (mirrors variableUpdate's guard).
    if (!isAttached(target, script->getClassName()))
    {
      continue;
    }

    m_engine->onCollision(targetStr.c_str(), script->getClassName().c_str(), otherStr.c_str(), eventCode);
  }
}

void ScriptSystem::dispatchSceneEvent(SceneAsset& scene,
                                      const SceneEvent event,
                                      const uuids::uuid* object,
                                      const int slot) const
{
  if (!m_engine || m_startedSceneScripts.empty())
  {
    return;
  }

  const auto sceneUUID = scene.getUUID();
  const auto sceneStr = uuids::to_string(sceneUUID);
  const auto objectStr = object ? uuids::to_string(*object) : std::string();

  // Held for every event kind: a handler that spawns lands in the pending list rather than the live one, so
  // the app's next structural flush replicates it once instead of a snapshot and an objectSpawned both
  // carrying it.
  const ObjectManager::ScriptPassGuard scriptPassGuard(*scene.getObjectManager());

  bool pointedBindings = false;

  // Iterates a copy of the list, since a handler could reach code that edits it.
  for (const auto& script : std::vector(scene.getScripts()))
  {
    const auto& className = script->getClassName();
    if (!isSceneScriptAttached(sceneUUID, className) ||
        !m_startedSceneScripts.contains(cacheKey(sceneUUID, className)))
    {
      continue;
    }

    if (!pointedBindings)
    {
      BindingContext::setObjectManager(scene.getObjectManager().get());
      pointedBindings = true;
    }

    m_engine->onSceneEvent(sceneStr.c_str(), className.c_str(), static_cast<int>(event), objectStr.c_str(), slot);
  }
}

void ScriptSystem::attachAll(ObjectManager& objectManager)
{
  ensureEngine();

  BindingContext::setObjectManager(&objectManager);

  // A script removed while the scene is stopped must not keep its instance either. This runs on every
  // scene edit's snapshot, so an orphan is usually already gone before the next tick sees it.
  detachOrphans(objectManager);

  attachAllObjects(objectManager);
}

void ScriptSystem::attachAll(SceneAsset& scene)
{
  ensureEngine();

  const auto objectManager = scene.getObjectManager();
  BindingContext::setObjectManager(objectManager.get());

  detachOrphans(*objectManager);
  detachSceneScriptOrphans(scene);

  for (const auto& script : scene.getScripts())
  {
    if (!isSceneScriptAttached(scene.getUUID(), script->getClassName()))
    {
      attachSceneScript(scene, script);
    }
  }

  attachAllObjects(*objectManager);
}

void ScriptSystem::attachAllObjects(ObjectManager& objectManager)
{
  for (const auto& object : objectManager.getAllObjects())
  {
    for (const auto& scriptComponent : object->getScripts())
    {
      const auto script = std::dynamic_pointer_cast<Script>(scriptComponent);
      if (!script)
      {
        continue;
      }

      if (!isAttached(object->getUUID(), script->getClassName()))
      {
        attach(*object, script);
      }
    }
  }
}

void ScriptSystem::syncFieldsToData(const ObjectManager& objectManager) const
{
  if (!m_engine)
  {
    return;
  }

  for (const auto& object : objectManager.getAllObjects())
  {
    for (const auto& scriptComponent : object->getScripts())
    {
      const auto script = std::dynamic_pointer_cast<Script>(scriptComponent);
      if (!script)
      {
        continue;
      }

      if (isAttached(object->getUUID(), script->getClassName()))
      {
        syncScriptBlob(object->getUUID(), script);
      }
    }
  }
}

void ScriptSystem::syncFieldsToData(const SceneAsset& scene) const
{
  if (!m_engine)
  {
    return;
  }

  for (const auto& script : scene.getScripts())
  {
    if (isSceneScriptAttached(scene.getUUID(), script->getClassName()))
    {
      syncScriptBlob(scene.getUUID(), script);
    }
  }

  syncFieldsToData(*scene.getObjectManager());
}

void ScriptSystem::syncScriptBlob(const uuids::uuid& ownerUUID, const std::shared_ptr<Script>& script) const
{
  const auto className = script->getClassName();
  const auto ownerStr = uuids::to_string(ownerUUID);

  // A missing or faulted instance reads back defaults, which would overwrite the saved values.
  if (!m_engine->isHealthy(ownerStr.c_str(), className.c_str()))
  {
    return;
  }

  auto fields = readFieldsFromInstance(ownerUUID, className);

  // A getter that throws mid-read faults the instance and the rest read back as defaults.
  if (!m_engine->isHealthy(ownerStr.c_str(), className.c_str()))
  {
    return;
  }

  script->setFields(fields);
}

void ScriptSystem::applyScriptFieldEdit(const uuids::uuid& objectUUID,
                                        const std::string& className,
                                        const nlohmann::json& fields) const
{
  if (!m_engine || !isAttached(objectUUID, className))
  {
    return;
  }

  writeFieldsToInstance(objectUUID, className, fields);
}

void ScriptSystem::applySceneScriptFieldEdit(const uuids::uuid& sceneUUID,
                                             const std::string& className,
                                             const nlohmann::json& fields) const
{
  if (!m_engine || !isSceneScriptAttached(sceneUUID, className))
  {
    return;
  }

  writeFieldsToInstance(sceneUUID, className, fields);
}

bool ScriptSystem::pollScriptChanges(SceneAsset& scene, const float dt)
{
  ensureEngine();

  const auto objectManager = scene.getObjectManager();
  BindingContext::setObjectManager(objectManager.get());

  if (!checkForScriptChanges([&] { syncFieldsToData(scene); }, dt))
  {
    return false;
  }

  // Attached but not started, so a class that just compiled exposes its fields to the next snapshot while
  // the scene stays stopped.
  attachAll(scene);
  syncFieldsToData(scene);

  return true;
}

bool ScriptSystem::checkForScriptChanges(const std::function<void()>& syncFields, const float dt)
{
  constexpr float minimumSnapshotInterval = 1.0f / 2.0f; // 2 checks every second

  m_timeSinceLastSnapshot += dt;

  if (m_timeSinceLastSnapshot < minimumSnapshotInterval)
  {
    return false;
  }

  m_timeSinceLastSnapshot = 0.0f;

  auto now = takeSnapshot();
  if (now == m_scriptsSnapshot)
  {
    return false;
  }

  Log::info(LogCategory::script, "Change detected - reloading scripts...");
  try
  {
    // Preserve live field values across the reload: read them back into each Script's data blob, then
    // recompile. The new instances are recreated lazily on the next fixedUpdate, which pushes the
    // refreshed blob back in.
    syncFields();

    const bool replaced = m_engine->reloadScripts();

    // Recorded even on a failed compile so the same broken sources wait for the next edit; the catch below
    // keeps the old snapshot, since a thrown reload says nothing about the sources and is worth retrying.
    m_scriptsSnapshot = std::move(now);

    if (!replaced)
    {
      return false;
    }

    m_attached.clear();
    m_attachedIndex.clear();
    m_fieldCache.clear();
    m_started.clear();
    m_unresolved.clear();
    m_attachedSceneScripts.clear();
    m_startedSceneScripts.clear();
    m_unresolvedSceneScripts.clear();

    Log::info(LogCategory::script, "Reload successful.");

    return true;
  }
  catch (const std::exception& ex)
  {
    Log::error(LogCategory::script,
               std::string("Script hot-reload failed: ") + ex.what() + " - continuing with previous scripts.");
  }

  return false;
}

void ScriptSystem::attach(const Object& object, const std::shared_ptr<Script>& script)
{
  const auto uuid = object.getUUID();
  const auto className = script->getClassName();
  const auto key = cacheKey(uuid, className);

  if (m_attached.contains(key) || m_unresolved.isUnresolved(uuid, className, script.get()))
  {
    return;
  }

  const auto uuidStr = uuids::to_string(uuid);

  if (!m_engine->attachScript(uuidStr.c_str(), className.c_str()))
  {
    if (m_unresolved.add(uuid, className, script))
    {
      Log::warn(LogCategory::script,
                "Script '" + className + "' on object " + uuidStr +
                " could not be instantiated (missing class, failed compile, or throwing constructor); "
                "its saved field values are kept.");
    }

    return;
  }

  m_attached.emplace(key, AttachedScript{ uuid, className, script });
  m_attachedIndex.attach(uuid, className);

  // Cache the instance's exposed fields (name/type) so we can read them back later for snapshots.
  auto& fields = m_fieldCache[key];
  fields.clear();
  for (const auto& f : nlohmann::json::parse(m_engine->getExposedFields(uuidStr.c_str(), className.c_str())))
  {
    fields.push_back({ f.at("name").get<std::string>(), f.at("type").get<std::string>() });
  }

  writeFieldsToInstance(uuid, className, script->getFields());
}

void ScriptSystem::detach(const uuids::uuid& uuid, const std::string& className)
{
  m_engine->detachScript(uuids::to_string(uuid).c_str(), className.c_str());

  const auto key = cacheKey(uuid, className);
  m_attached.erase(key);
  m_attachedIndex.detach(uuid, className);
  m_fieldCache.erase(key);
  m_started.erase(key);
}

void ScriptSystem::detachOrphans(const ObjectManager& objectManager)
{
  if (m_attached.empty() && m_unresolved.empty())
  {
    return;
  }

  std::unordered_map<std::string, const Component*> liveScripts;
  liveScripts.reserve(m_attached.size());

  for (const auto& object : objectManager.getAllObjects())
  {
    for (const auto& scriptComponent : object->getScripts())
    {
      const auto script = std::dynamic_pointer_cast<Script>(scriptComponent);
      if (!script)
      {
        continue;
      }

      liveScripts.emplace(cacheKey(object->getUUID(), script->getClassName()), script.get());
    }
  }

  m_unresolved.prune([&liveScripts](const uuids::uuid& uuid, const std::string& className) -> const Component*
  {
    const auto live = liveScripts.find(cacheKey(uuid, className));
    return live == liveScripts.end() ? nullptr : live->second;
  });

  // Collected first because detach() erases from m_attached.
  std::vector<std::pair<std::string, AttachedScript>> orphans;
  for (const auto& [key, attached] : m_attached)
  {
    // A different component under the same key is a replacement, not a survivor: the instance belongs
    // to the Script that is gone, so it is stopped and the new component attaches its own.
    const auto live = liveScripts.find(key);
    if (live == liveScripts.end() || live->second != attached.component.lock().get())
    {
      orphans.emplace_back(key, attached);
    }
  }

  for (const auto& [key, orphan] : orphans)
  {
    if (m_started.contains(key))
    {
      m_engine->stop(uuids::to_string(orphan.uuid).c_str(), orphan.className.c_str());
    }

    detach(orphan.uuid, orphan.className);
  }
}

void ScriptSystem::startIfNeeded(const uuids::uuid& uuid, const std::string& className)
{
  if (!m_started.insert(cacheKey(uuid, className)).second)
  {
    // Already started - m_started.insert only returns true the first time a key is added.
    return;
  }

  m_engine->start(uuids::to_string(uuid).c_str(), className.c_str());
}

void ScriptSystem::attachSceneScript(const SceneAsset& scene, const std::shared_ptr<Script>& script)
{
  const auto uuid = scene.getUUID();
  const auto className = script->getClassName();
  const auto key = cacheKey(uuid, className);

  if (m_attachedSceneScripts.contains(key) || m_unresolvedSceneScripts.isUnresolved(uuid, className, script.get()))
  {
    return;
  }

  const auto uuidStr = uuids::to_string(uuid);

  if (!m_engine->attachSceneScript(uuidStr.c_str(), className.c_str()))
  {
    if (m_unresolvedSceneScripts.add(uuid, className, script))
    {
      Log::warn(LogCategory::script,
                "Scene script '" + className + "' in scene '" + scene.getName() +
                "' could not be instantiated (missing class, not a SceneScript, failed compile, or throwing "
                "constructor); its saved field values are kept.");
    }

    return;
  }

  m_attachedSceneScripts.emplace(key, AttachedSceneScript{ uuid, className, script });

  auto& fields = m_fieldCache[key];
  fields.clear();
  for (const auto& f : nlohmann::json::parse(m_engine->getExposedFields(uuidStr.c_str(), className.c_str())))
  {
    fields.push_back({ f.at("name").get<std::string>(), f.at("type").get<std::string>() });
  }

  writeFieldsToInstance(uuid, className, script->getFields());
}

void ScriptSystem::detachSceneScript(const uuids::uuid& sceneUUID, const std::string& className)
{
  m_engine->detachScript(uuids::to_string(sceneUUID).c_str(), className.c_str());

  const auto key = cacheKey(sceneUUID, className);
  m_attachedSceneScripts.erase(key);
  m_fieldCache.erase(key);
  m_startedSceneScripts.erase(key);
}

void ScriptSystem::detachSceneScriptOrphans(const SceneAsset& scene)
{
  if (m_attachedSceneScripts.empty() && m_unresolvedSceneScripts.empty())
  {
    return;
  }

  std::unordered_map<std::string, const Component*> liveScripts;
  for (const auto& script : scene.getScripts())
  {
    liveScripts.emplace(cacheKey(scene.getUUID(), script->getClassName()), script.get());
  }

  // A key under another scene's uuid is never live here, so a scene switch orphans the old scene's scripts.
  m_unresolvedSceneScripts.prune([&liveScripts](const uuids::uuid& uuid, const std::string& className)
    -> const Component*
  {
    const auto live = liveScripts.find(cacheKey(uuid, className));
    return live == liveScripts.end() ? nullptr : live->second;
  });

  // Collected first because detachSceneScript() erases from m_attachedSceneScripts.
  std::vector<std::pair<std::string, AttachedSceneScript>> orphans;
  for (const auto& [key, attached] : m_attachedSceneScripts)
  {
    const auto live = liveScripts.find(key);
    if (live == liveScripts.end() || live->second != attached.component.lock().get())
    {
      orphans.emplace_back(key, attached);
    }
  }

  for (const auto& [key, orphan] : orphans)
  {
    if (m_startedSceneScripts.contains(key))
    {
      m_engine->stop(uuids::to_string(orphan.sceneUUID).c_str(), orphan.className.c_str());
    }

    detachSceneScript(orphan.sceneUUID, orphan.className);
  }
}

void ScriptSystem::startSceneScriptIfNeeded(const uuids::uuid& sceneUUID, const std::string& className)
{
  if (!m_startedSceneScripts.insert(cacheKey(sceneUUID, className)).second)
  {
    return;
  }

  m_engine->start(uuids::to_string(sceneUUID).c_str(), className.c_str());
}

bool ScriptSystem::isSceneScriptAttached(const uuids::uuid& sceneUUID, const std::string& className) const
{
  return m_attachedSceneScripts.contains(cacheKey(sceneUUID, className));
}

void ScriptSystem::writeFieldsToInstance(const uuids::uuid& uuid,
                                         const std::string& className,
                                         const nlohmann::json& fields) const
{
  if (!fields.is_array())
  {
    return;
  }

  const auto cached = m_fieldCache.find(cacheKey(uuid, className));
  if (cached == m_fieldCache.end())
  {
    return;
  }

  const auto uuidStr = uuids::to_string(uuid);

  for (const auto& field : fields)
  {
    const std::string* cachedType = nullptr;
    if (field.contains("name") && field.at("name").is_string())
    {
      const auto editedName = field.at("name").get<std::string>();
      for (const auto& exposed : cached->second)
      {
        if (exposed.name == editedName)
        {
          cachedType = &exposed.type;
          break;
        }
      }
    }

    // An editor's field list can be stale, or hostile: a tag that disagrees with what the instance
    // really exposes would otherwise reach a setter and either throw here or fault the script.
    if (const auto reason = scripting::rejectFieldEdit(field, cachedType); !reason.empty())
    {
      Log::warn(LogCategory::script, "Refused a field edit on " + uuidStr + " (" + className + "): " + reason);
      continue;
    }

    const std::string name = field.at("name");
    const std::string type = field.at("type");
    const auto fieldName = name.c_str();

    if (type == "float")
    {
      m_engine->setFieldFloat(uuidStr.c_str(), className.c_str(), fieldName, field.at("value").get<float>());
    }
    else if (type == "int")
    {
      m_engine->setFieldInt(uuidStr.c_str(), className.c_str(), fieldName, field.at("value").get<int>());
    }
    else if (type == "bool")
    {
      m_engine->setFieldBool(uuidStr.c_str(), className.c_str(), fieldName, field.at("value").get<bool>());
    }
    else if (type == "string")
    {
      const auto value = field.at("value").get<std::string>();
      m_engine->setFieldString(uuidStr.c_str(), className.c_str(), fieldName, value.c_str());
    }
    else if (type == "vector3")
    {
      // Stored as a [x, y, z] JSON array (see readFieldsFromInstance / ScriptEditor).
      const auto& value = field.at("value");
      if (value.is_array() && value.size() == 3)
      {
        m_engine->setFieldVector3(uuidStr.c_str(), className.c_str(), fieldName,
          value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>());
      }
    }
  }
}

nlohmann::json ScriptSystem::readFieldsFromInstance(const uuids::uuid& uuid,
                                                    const std::string& className) const
{
  auto fields = nlohmann::json::array();

  const auto it = m_fieldCache.find(cacheKey(uuid, className));
  if (it == m_fieldCache.end())
  {
    return fields;
  }

  const auto uuidStr = uuids::to_string(uuid);

  for (const auto& field : it->second)
  {
    const auto fieldName = field.name.c_str();

    nlohmann::json entry = {
      { "name", field.name },
      { "type", field.type },
      { "value", "" }
    };

    if (field.type == "float")
    {
      entry["value"] = m_engine->getFieldFloat(uuidStr.c_str(), className.c_str(), fieldName);
    }
    else if (field.type == "int")
    {
      entry["value"] = m_engine->getFieldInt(uuidStr.c_str(), className.c_str(), fieldName);
    }
    else if (field.type == "bool")
    {
      entry["value"] = m_engine->getFieldBool(uuidStr.c_str(), className.c_str(), fieldName);
    }
    else if (field.type == "string")
    {
      entry["value"] = m_engine->getFieldString(uuidStr.c_str(), className.c_str(), fieldName);
    }
    else if (field.type == "vector3")
    {
      float x = 0.0f, y = 0.0f, z = 0.0f;
      m_engine->getFieldVector3(uuidStr.c_str(), className.c_str(), fieldName, x, y, z);
      entry["value"] = { x, y, z };
    }

    fields.push_back(std::move(entry));
  }

  return fields;
}

bool ScriptSystem::isAttached(const uuids::uuid& uuid, const std::string& className) const
{
  return m_attachedIndex.isAttached(uuid, className);
}

std::string ScriptSystem::cacheKey(const uuids::uuid& uuid, const std::string& className)
{
  return uuids::to_string(uuid) + "_" + className;
}

ScriptSystem::ScriptsSnapshot ScriptSystem::takeSnapshot() const
{
  ScriptsSnapshot times;
  std::error_code ec;
  for (auto& entry : std::filesystem::recursive_directory_iterator(m_userScriptsDir, ec))
  {
    if (entry.is_regular_file(ec))
    {
      times[entry.path().string()] = entry.last_write_time(ec);
    }
  }

  return times;
}
