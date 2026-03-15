#include "ScriptManager.h"
#include <iostream>

ScriptManager::ScriptManager(ECS3D* ecs)
  : m_ecs(ecs)
{
  m_scriptEngine.init(m_scriptBridgeDir, m_userScriptsDir);

  m_scriptsSnapshot = takeSnapshot();
}

void ScriptManager::checkForScriptChanges()
{
  if (isSnapshotCurrent())
  {
    return;
  }

  std::cout << "\n[hot-reload] Change detected - reloading scripts..." << std::endl;
  try
  {
    m_scriptEngine.reloadScripts();
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

ScriptsSnapshot ScriptManager::takeSnapshot() const
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

bool ScriptManager::isSnapshotCurrent()
{
  if (auto now = takeSnapshot(); now != m_scriptsSnapshot)
  {
    m_scriptsSnapshot = std::move(now);
    return false;
  }

  return true;
}
