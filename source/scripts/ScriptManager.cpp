#include "ScriptManager.h"

#include <iostream>

static ScriptsSnapshot snapshot(const std::filesystem::path& dir)
{
  ScriptsSnapshot times;
  std::error_code ec;
  for (auto& entry : std::filesystem::recursive_directory_iterator(dir, ec))
  {
    if (entry.is_regular_file(ec))
    {
      times[entry.path().string()] = entry.last_write_time(ec);
    }
  }
  return times;
}

static bool changed(const std::filesystem::path& dir,
                    ScriptsSnapshot& prev)
{
  auto now = snapshot(dir);
  if (now != prev)
  {
    prev = std::move(now);
    return true;
  }
  return false;
}

ScriptManager::ScriptManager(ECS3D* ecs)
  : m_ecs(ecs)
{
  m_scriptEngine.Init(m_scriptBridgeDir, m_userScriptsDir);

  m_scriptTimes = snapshot(m_userScriptsDir);
}

void ScriptManager::checkForScriptChanges()
{
  if (changed(m_userScriptsDir, m_scriptTimes))
  {
    std::cout << "\n[hot-reload] Change detected - reloading scripts..." << std::endl;
    try
    {
      m_scriptEngine.ReloadScripts();
      std::cout << "[hot-reload] Reload successful." << std::endl;
    }
    catch (const std::exception& ex)
    {
      std::cerr << "[hot-reload] Reload failed: " << ex.what() << " - continuing with previous scripts." << std::endl;
    }
  }
}

void ScriptManager::fixedUpdate(float dt)
{
  // m_scriptEngine->fixedUpdate(dt);
}

void ScriptManager::variableUpdate()
{
  // m_scriptEngine->variableUpdate();
}
