#include "ScriptManager.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>

const std::string SCRIPT_BRIDGE_DIR = "scripts/ScriptBridge";
const std::string USER_SCRIPTS_DIR = "scripts/UserScripts";

ScriptManager::ScriptManager(ECS3D* ecs)
  : m_ecs(ecs)
{
  m_scriptEngine.init(ecs, SCRIPT_BRIDGE_DIR, USER_SCRIPTS_DIR);

  m_scriptsSnapshot = takeSnapshot();
}

void ScriptManager::checkForScriptChanges()
{
  auto now = takeSnapshot();
  if (now == m_scriptsSnapshot)
  {
    return;
  }

  std::cout << "\n[hot-reload] Change detected - reloading scripts..." << std::endl;
  try
  {
    m_fieldCache.clear();
    m_scriptEngine.reloadScripts();
    m_scriptsSnapshot = std::move(now);
    std::cout << "[hot-reload] Reload successful." << std::endl;
  }
  catch (const std::exception& ex)
  {
    std::cerr << "[hot-reload] Reload failed: " << ex.what() << " - continuing with previous scripts." << std::endl;
  }
}

void ScriptManager::attachScript(const uuids::uuid uuid,
                                 const char* className) const
{
  const auto uuidStr = uuids::to_string(uuid);
  m_scriptEngine.attachScript(uuidStr.c_str(), className);

  auto json = nlohmann::json::parse(m_scriptEngine.getExposedFields(uuidStr.c_str(), className));
  auto& fields = m_fieldCache[cacheKey(uuid, className)];
  fields.clear();
  for (const auto& f : json)
  {
    fields.push_back({ f["name"], f["displayName"], f["type"] });
  }
}

void ScriptManager::detachScript(const uuids::uuid uuid,
                                 const char* className) const
{
  const auto uuidStr = uuids::to_string(uuid);
  m_scriptEngine.detachScript(uuidStr.c_str(), className);

  m_fieldCache.erase(cacheKey(uuid, className));
}

void ScriptManager::fixedUpdate(const uuids::uuid uuid,
                                const char* className,
                                const float dt) const
{
  const auto uuidStr = uuids::to_string(uuid);
  m_scriptEngine.fixedUpdate(uuidStr.c_str(), className, dt);
}

void ScriptManager::variableUpdate(const uuids::uuid uuid,
                                   const char* className) const
{
  const auto uuidStr = uuids::to_string(uuid);
  m_scriptEngine.variableUpdate(uuidStr.c_str(), className);
}

const std::vector<ScriptManager::ExposedField>* ScriptManager::getExposedFields(const uuids::uuid uuid,
                                                                                const char* className) const
{
  const auto it = m_fieldCache.find(cacheKey(uuid, className));

  return it == m_fieldCache.end() ? nullptr : &it->second;
}

float ScriptManager::getFieldFloat(const uuids::uuid uuid,
                                   const char* className,
                                   const char* fieldName) const
{
  return m_scriptEngine.getFieldFloat(uuids::to_string(uuid).c_str(), className, fieldName);
}

int ScriptManager::getFieldInt(const uuids::uuid uuid,
                               const char* className,
                               const char* fieldName) const
{
  return m_scriptEngine.getFieldInt(uuids::to_string(uuid).c_str(), className, fieldName);
}

bool ScriptManager::getFieldBool(const uuids::uuid uuid,
                                 const char* className,
                                 const char* fieldName) const
{
  return m_scriptEngine.getFieldBool(uuids::to_string(uuid).c_str(), className, fieldName);
}

void ScriptManager::setFieldFloat(const uuids::uuid uuid,
                                  const char* className,
                                  const char* fieldName,
                                  const float value) const
{
  m_scriptEngine.setFieldFloat(uuids::to_string(uuid).c_str(), className, fieldName, value);
}

void ScriptManager::setFieldInt(const uuids::uuid uuid,
                                const char* className,
                                const char* fieldName,
                                const int value) const
{
  m_scriptEngine.setFieldInt(uuids::to_string(uuid).c_str(), className, fieldName, value);
}

void ScriptManager::setFieldBool(const uuids::uuid uuid,
                                 const char* className,
                                 const char* fieldName,
                                 const bool value) const
{
  m_scriptEngine.setFieldBool(uuids::to_string(uuid).c_str(), className, fieldName, value);
}

ScriptManager::ScriptsSnapshot ScriptManager::takeSnapshot()
{
  ScriptsSnapshot times;
  std::error_code ec;
  for (auto& entry : std::filesystem::recursive_directory_iterator(USER_SCRIPTS_DIR, ec))
  {
    if (entry.is_regular_file(ec))
    {
      times[entry.path().string()] = entry.last_write_time(ec);
    }
  }

  return times;
}

std::string ScriptManager::cacheKey(const uuids::uuid uuid,
                                    const char* className)
{
  return uuids::to_string(uuid) + "_" + className;
}
