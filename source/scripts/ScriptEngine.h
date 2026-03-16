#ifndef ECS3D_SCRIPTENGINE_H
#define ECS3D_SCRIPTENGINE_H

#include <string>

class ECS3D;

class ScriptEngine
{
public:
  ScriptEngine() = default;
  ~ScriptEngine();

  ScriptEngine(const ScriptEngine&) = delete;
  ScriptEngine& operator=(const ScriptEngine&) = delete;

  void init(ECS3D* ecs, const std::string& bridgeDir, const std::string& scriptDir);
  void reloadScripts() const;
  void shutdown();

  void fixedUpdate(float dt) const;
  void variableUpdate() const;

private:
  static void* LoadLib(const std::string& path);
  static void* GetSymbol(void* lib, const char* name);

  using fixedUpdateFn = void(*)(float);
  using variableUpdateFn = void(*)();
  using VoidFn = void(*)();
  using InitBridgeFn = void(*)(const char*);

  void* m_hostfxrLib = nullptr;
  void* m_hostContext = nullptr;
  bool m_initialized = false;

  fixedUpdateFn m_fixedUpdate = nullptr;
  variableUpdateFn m_variableUpdate = nullptr;
  VoidFn m_reload = nullptr;
};

#endif //ECS3D_SCRIPTENGINE_H