#ifndef SCRIPTENGINE_H
#define SCRIPTENGINE_H

#include <memory>
#include <string>

class ManagedHost;

// ScriptBridge ABI: resolves the bridge's [UnmanagedCallersOnly] entrypoints into native function
// pointers via ManagedHost, registers component bindings with the managed side, and forwards
// attach/start/stop/fixedUpdate + the exposed-field getters/setters. Server only.
class ScriptEngine {
public:
  explicit ScriptEngine(std::shared_ptr<ManagedHost> host);

  // bridgeDir holds ScriptBridge.dll (+ runtimeconfig); scriptDir holds the user .cs files the bridge
  // compiles. Resolves every entrypoint, registers bindings, then tells the bridge to compile.
  void init(const std::string& bridgeDir,
            const std::string& scriptDir);

  void reloadScripts() const;

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

  // Forward a collision event to one script instance. event matches CollisionEvent in ScriptSystem.h
  // (0 = enter, 1 = stay, 2 = exit); kept as a raw int here so the ABI layer stays free of scripting's
  // semantic enum.
  void onCollision(const char* uuid,
                   const char* className,
                   const char* otherUuid,
                   int event) const;

  [[nodiscard]] std::string getExposedFields(const char* uuid,
                                             const char* className) const;

  [[nodiscard]] float getFieldFloat(const char* uuid,
                                    const char* className,
                                    const char* fieldName) const;

  [[nodiscard]] int getFieldInt(const char* uuid,
                                const char* className,
                                const char* fieldName) const;

  [[nodiscard]] bool getFieldBool(const char* uuid,
                                  const char* className,
                                  const char* fieldName) const;

  void getFieldVector3(const char* uuid,
                       const char* className,
                       const char* fieldName,
                       float& x, float& y, float& z) const;

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

  void setFieldVector3(const char* uuid,
                       const char* className,
                       const char* fieldName,
                       float x, float y, float z) const;

  [[nodiscard]] bool isInitialized() const { return m_initialized; }

private:
  using AttachScriptFn = void(*)(const char*, const char*);
  using DetachScriptFn = void(*)(const char*, const char*);
  using StartFn = void(*)(const char*, const char*);
  using StopFn = void(*)(const char*, const char*);
  using FixedUpdateFn = void(*)(const char*, const char*, float);
  using VariableUpdateFn = void(*)(const char*, const char*);
  using OnCollisionFn = void(*)(const char*, const char*, const char*, int);
  using VoidFn = void(*)();
  using InitBridgeFn = void(*)(const char*);

  using GetExposedFieldsFn = char*(*)(const char*, const char*);
  using FreeStringFn = void(*)(char*);

  using GetFieldFloatFn = float(*)(const char*, const char*, const char*);
  using GetFieldIntFn = int(*)(const char*, const char*, const char*);
  using GetFieldBoolFn = bool(*)(const char*, const char*, const char*);
  using GetFieldVector3Fn = void(*)(const char*, const char*, const char*, float*, float*, float*);

  using SetFieldFloatFn = void(*)(const char*, const char*, const char*, float);
  using SetFieldIntFn = void(*)(const char*, const char*, const char*, int);
  using SetFieldBoolFn = void(*)(const char*, const char*, const char*, bool);
  using SetFieldVector3Fn = void(*)(const char*, const char*, const char*, float, float, float);

  std::shared_ptr<ManagedHost> m_host;
  bool m_initialized = false;

  AttachScriptFn m_attachScript = nullptr;
  DetachScriptFn m_detachScript = nullptr;
  StartFn m_start = nullptr;
  StopFn m_stop = nullptr;
  FixedUpdateFn m_fixedUpdate = nullptr;
  VariableUpdateFn m_variableUpdate = nullptr;
  OnCollisionFn m_onCollision = nullptr;
  VoidFn m_reload = nullptr;

  GetExposedFieldsFn m_getExposedFields = nullptr;
  FreeStringFn m_freeString = nullptr;

  GetFieldFloatFn m_getFieldFloat = nullptr;
  GetFieldIntFn m_getFieldInt = nullptr;
  GetFieldBoolFn m_getFieldBool = nullptr;
  GetFieldVector3Fn m_getFieldVector3 = nullptr;

  SetFieldFloatFn m_setFieldFloat = nullptr;
  SetFieldIntFn m_setFieldInt = nullptr;
  SetFieldBoolFn m_setFieldBool = nullptr;
  SetFieldVector3Fn m_setFieldVector3 = nullptr;

  void registerBindings(const std::string& assemblyPath,
                        const std::string& typeName) const;
};



#endif //SCRIPTENGINE_H
