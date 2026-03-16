#ifndef ECS3D_SCRIPTENGINE_H
#define ECS3D_SCRIPTENGINE_H

#include <functional>
#include <string>

class ECS3D;

class ScriptEngine
{
public:
  ScriptEngine() = default;
  ScriptEngine(const ScriptEngine&) = delete;
  ScriptEngine& operator=(const ScriptEngine&) = delete;

  ~ScriptEngine();

  void init(ECS3D* ecs,
            const std::string& bridgeDir,
            const std::string& scriptDir);

  void reloadScripts() const;

  void shutdown();

  void attachScript(const char* uuid,
                    const char* className) const;

  void detachScript(const char* uuid,
                    const char* className) const;

  void fixedUpdate(const char* uuid,
                   const char* className,
                   float dt) const;

  void variableUpdate(const char* uuid,
                      const char* className) const;

private:
  using attachScriptFn = void(*)(const char*, const char*);
  using detachScriptFn = void(*)(const char*, const char*);
  using fixedUpdateFn = void(*)(const char*, const char*, float);
  using variableUpdateFn = void(*)(const char*, const char*);
  using VoidFn = void(*)();
  using InitBridgeFn = void(*)(const char*);

  void* m_hostfxrLib = nullptr;
  void* m_hostContext = nullptr;
  bool m_initialized = false;

  attachScriptFn m_attachScript = nullptr;
  detachScriptFn m_detachScript = nullptr;

  fixedUpdateFn m_fixedUpdate = nullptr;
  variableUpdateFn m_variableUpdate = nullptr;
  VoidFn m_reload = nullptr;

  static void* LoadLib(const std::string& path);

  static void* GetSymbol(void* lib,
                         const char* name);

  static void initBindings(ECS3D* ecs);
  static void registerBindings(const std::function<void(const char*, void**)>& loadFn);
};

#endif //ECS3D_SCRIPTENGINE_H