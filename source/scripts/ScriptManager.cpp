#include "ScriptManager.h"
#include <iostream>
#include <string>

const std::string SCRIPT_BRIDGE_DIR = "scripts/ScriptBridge";
const std::string USER_SCRIPTS_DIR = "scripts/UserScripts";

ScriptManager::ScriptManager(ECS3D* ecs)
  : m_ecs(ecs)
{
  m_scriptEngine.init(SCRIPT_BRIDGE_DIR, USER_SCRIPTS_DIR);

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
    m_scriptEngine.reloadScripts();
    m_scriptsSnapshot = std::move(now);
    std::cout << "[hot-reload] Reload successful." << std::endl;
  }
  catch (const std::exception& ex)
  {
    std::cerr << "[hot-reload] Reload failed: " << ex.what() << " - continuing with previous scripts." << std::endl;
  }
}

void ScriptManager::fixedUpdate(const float dt) const
{
  m_scriptEngine.fixedUpdate(dt);
}

void ScriptManager::variableUpdate() const
{
  m_scriptEngine.variableUpdate();
}

ScriptsSnapshot ScriptManager::takeSnapshot()
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
