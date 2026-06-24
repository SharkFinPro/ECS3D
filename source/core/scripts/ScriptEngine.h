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

  void start(const char* uuid,
             const char* className) const;

  void stop(const char* uuid,
            const char* className) const;

  void fixedUpdate(const char* uuid,
                   const char* className,
                   float dt) const;

  void variableUpdate(const char* uuid,
                      const char* className) const;

  std::string getExposedFields(const char* uuid,
                               const char* className) const;

  float getFieldFloat(const char* uuid,
                      const char* className,
                      const char* fieldName) const;

  int getFieldInt(const char* uuid,
                  const char* className,
                  const char* fieldName) const;

  bool getFieldBool(const char* uuid,
                    const char* className,
                    const char* fieldName) const;

  void setFieldFloat(const char* uuid,
                     const char* className,
                     const char* fieldName,
                     float value) const;

  void setFieldInt(const char* uuid,
                   const char* className,
                   const char* fieldName,
                   int value) const;

  void setFieldBool(const char* uuid,
                    const char* className,
                    const char* fieldName,
                    bool value) const;

private:
  using attachScriptFn = void(*)(const char*, const char*);
  using detachScriptFn = void(*)(const char*, const char*);

  using startFn = void(*)(const char*, const char*);
  using stopFn = void(*)(const char*, const char*);

  using fixedUpdateFn = void(*)(const char*, const char*, float);
  using variableUpdateFn = void(*)(const char*, const char*);
  using VoidFn = void(*)();
  using InitBridgeFn = void(*)(const char*);

  using GetExposedFieldsFn = char*(*)(const char*, const char*);
  using FreeStringFn = void(*)(char*);

  using GetFieldFloatFn = float(*)(const char*, const char*, const char*);
  using GetFieldIntFn = int(*)(const char*, const char*, const char*);
  using GetFieldBoolFn = bool(*)(const char*, const char*, const char*);

  using SetFieldFloatFn = void(*)(const char*, const char*, const char*, float);
  using SetFieldIntFn = void(*)(const char*, const char*, const char*, int);
  using SetFieldBoolFn = void(*)(const char*, const char*, const char*, bool);

  void* m_hostfxrLib = nullptr;
  void* m_hostContext = nullptr;
  bool m_initialized = false;

  attachScriptFn m_attachScript = nullptr;
  detachScriptFn m_detachScript = nullptr;

  startFn m_start = nullptr;
  stopFn m_stop = nullptr;

  fixedUpdateFn m_fixedUpdate = nullptr;
  variableUpdateFn m_variableUpdate = nullptr;
  VoidFn m_reload = nullptr;

  GetExposedFieldsFn m_getExposedFields = nullptr;
  FreeStringFn m_freeString = nullptr;

  GetFieldFloatFn m_getFieldFloat = nullptr;
  GetFieldIntFn m_getFieldInt = nullptr;
  GetFieldBoolFn m_getFieldBool = nullptr;

  SetFieldFloatFn m_setFieldFloat = nullptr;
  SetFieldIntFn m_setFieldInt = nullptr;
  SetFieldBoolFn m_setFieldBool = nullptr;

  static void* LoadLib(const std::string& path);

  static void* GetSymbol(void* lib,
                         const char* name);

  static void initBindings(ECS3D* ecs);
  static void registerBindings(const std::function<void(const char*, void**)>& loadFn);
};

#endif //ECS3D_SCRIPTENGINE_H