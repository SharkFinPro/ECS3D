#include "ScriptSystem.h"
#include "ScriptEngine.h"
#include "bindings/BindingContext.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/Script.h>
#include <nlohmann/json.hpp>
#include <iostream>

namespace {
  // Published next to the executable by ecs3d_add_managed_assembly / copied by CMake. Relative paths
  // assume CWD = exe dir, matching the rest of the engine's asset loading.
  const std::string kScriptBridgeDir = "scripts/ScriptBridge";
  const std::string kUserScriptsDir = "scripts/UserScripts";
}

ScriptSystem::ScriptSystem(std::shared_ptr<ManagedHost> host)
  : m_host(std::move(host))
{}

ScriptSystem::~ScriptSystem() = default;

void ScriptSystem::ensureEngine()
{
  if (m_engine)
  {
    return;
  }

  // Compiling the user scripts (the bridge's init) is heavy, so the engine is built once on first
  // use rather than in the constructor. Init into a local first so a failure leaves m_engine null and
  // the next call retries instead of holding a half-initialized engine.
  auto engine = std::make_unique<ScriptEngine>(m_host);
  engine->init(kScriptBridgeDir, kUserScriptsDir);

  m_engine = std::move(engine);

  m_scriptsSnapshot = takeSnapshot();
}

void ScriptSystem::start(ObjectManager& objectManager)
{
  ensureEngine();

  BindingContext::setObjectManager(&objectManager);

  for (const auto& object : objectManager.getAllObjects())
  {
    for (const auto& scriptComponent : object->getScripts())
    {
      const auto script = std::dynamic_pointer_cast<Script>(scriptComponent);
      if (!script)
      {
        continue;
      }

      attach(*object, *script);
      m_engine->start(uuids::to_string(object->getUUID()).c_str(), script->getClassName().c_str());
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
}

void ScriptSystem::fixedUpdate(ObjectManager& objectManager, const float dt)
{
  ensureEngine();

  // Point the C-ABI bindings at the scene so script callbacks (applyForce, move, ...) can resolve a
  // uuid to its object while they run.
  BindingContext::setObjectManager(&objectManager);

  checkForScriptChanges(objectManager, dt);

  for (const auto& object : objectManager.getAllObjects())
  {
    for (const auto& scriptComponent : object->getScripts())
    {
      const auto script = std::dynamic_pointer_cast<Script>(scriptComponent);
      if (!script)
      {
        continue;
      }

      // Lazily (re)create the instance if needed - this is what restores instances after a hot reload,
      // pushing the (refreshed) field blob back in. Matches the old Script::fixedUpdate lazy attach.
      if (!isAttached(object->getUUID(), script->getClassName()))
      {
        attach(*object, *script);
      }

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

  for (const auto& object : objectManager.getAllObjects())
  {
    for (const auto& scriptComponent : object->getScripts())
    {
      const auto script = std::dynamic_pointer_cast<Script>(scriptComponent);
      if (!script)
      {
        continue;
      }

      // Only run instances that have already been attached + started (fixedUpdate/start own attachment).
      if (!isAttached(object->getUUID(), script->getClassName()))
      {
        continue;
      }

      m_engine->variableUpdate(uuids::to_string(object->getUUID()).c_str(), script->getClassName().c_str());
    }
  }
}

void ScriptSystem::syncFieldsToData(ObjectManager& objectManager)
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

      const auto uuid = object->getUUID();
      const auto className = script->getClassName();

      if (!isAttached(uuid, className))
      {
        continue;
      }

      script->setFields(readFieldsFromInstance(uuid, className));
    }
  }
}

void ScriptSystem::checkForScriptChanges(ObjectManager& objectManager, const float dt)
{
  constexpr float minimumSnapshotInterval = 1.0f / 2.0f; // 2 checks every second

  m_timeSinceLastSnapshot += dt;

  if (m_timeSinceLastSnapshot < minimumSnapshotInterval)
  {
    return;
  }

  m_timeSinceLastSnapshot = 0.0f;

  auto now = takeSnapshot();
  if (now == m_scriptsSnapshot)
  {
    return;
  }

  std::cout << "\n[hot-reload] Change detected - reloading scripts..." << std::endl;
  try
  {
    // Preserve live field values across the reload: read them back into each Script's data blob, then
    // recompile. The new instances are recreated lazily on the next fixedUpdate, which pushes the
    // refreshed blob back in.
    syncFieldsToData(objectManager);

    m_engine->reloadScripts();

    m_attached.clear();
    m_fieldCache.clear();

    m_scriptsSnapshot = std::move(now);

    std::cout << "[hot-reload] Reload successful." << std::endl;
  }
  catch (const std::exception& ex)
  {
    std::cerr << "[hot-reload] Reload failed: " << ex.what() << " - continuing with previous scripts." << std::endl;
  }
}

void ScriptSystem::attach(const Object& object, const Script& script)
{
  const auto uuid = object.getUUID();
  const auto className = script.getClassName();
  const auto key = cacheKey(uuid, className);

  if (m_attached.contains(key))
  {
    return;
  }

  const auto uuidStr = uuids::to_string(uuid);

  m_engine->attachScript(uuidStr.c_str(), className.c_str());
  m_attached.insert(key);

  // Cache the instance's exposed fields (name/type) so we can read them back later for snapshots.
  auto& fields = m_fieldCache[key];
  fields.clear();
  for (const auto& f : nlohmann::json::parse(m_engine->getExposedFields(uuidStr.c_str(), className.c_str())))
  {
    fields.push_back({ f.at("name").get<std::string>(), f.at("type").get<std::string>() });
  }

  // Push the Script's saved field blob into the fresh instance (the old Script::loadFromJSON path).
  writeFieldsToInstance(uuid, className, script.getFields());
}

void ScriptSystem::detach(const uuids::uuid& uuid, const std::string& className)
{
  m_engine->detachScript(uuids::to_string(uuid).c_str(), className.c_str());

  const auto key = cacheKey(uuid, className);
  m_attached.erase(key);
  m_fieldCache.erase(key);
}

void ScriptSystem::writeFieldsToInstance(const uuids::uuid& uuid,
                                         const std::string& className,
                                         const nlohmann::json& fields) const
{
  if (!fields.is_array())
  {
    return;
  }

  const auto uuidStr = uuids::to_string(uuid);

  for (const auto& field : fields)
  {
    if (!field.contains("name") || !field.contains("type") || !field.contains("value"))
    {
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

    fields.push_back(std::move(entry));
  }

  return fields;
}

bool ScriptSystem::isAttached(const uuids::uuid& uuid, const std::string& className) const
{
  return m_attached.contains(cacheKey(uuid, className));
}

std::string ScriptSystem::cacheKey(const uuids::uuid& uuid, const std::string& className)
{
  return uuids::to_string(uuid) + "_" + className;
}

ScriptSystem::ScriptsSnapshot ScriptSystem::takeSnapshot()
{
  ScriptsSnapshot times;
  std::error_code ec;
  for (auto& entry : std::filesystem::recursive_directory_iterator(kUserScriptsDir, ec))
  {
    if (entry.is_regular_file(ec))
    {
      times[entry.path().string()] = entry.last_write_time(ec);
    }
  }

  return times;
}
