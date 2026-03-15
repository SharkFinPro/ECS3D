#ifndef ECS3D_SCRIPTMANAGER_H
#define ECS3D_SCRIPTMANAGER_H


#include "ScriptEngine.h"
#include <filesystem>
#include <unordered_map>

class ECS3D;

using ScriptsSnapshot = std::unordered_map<std::string, std::filesystem::file_time_type>;

class ScriptManager {
public:
  explicit ScriptManager(ECS3D* ecs);

  void checkForScriptChanges();

  void fixedUpdate(float dt) const;
  void variableUpdate() const;

private:
  ECS3D* m_ecs;
  ScriptEngine m_scriptEngine;

  ScriptsSnapshot m_scriptsSnapshot;

  const std::string m_scriptBridgeDir = "scripts/ScriptBridge";
  const std::string m_userScriptsDir = "scripts/UserScripts";

  [[nodiscard]] ScriptsSnapshot takeSnapshot() const;

  [[nodiscard]] bool isSnapshotCurrent();
};


#endif //ECS3D_SCRIPTMANAGER_H