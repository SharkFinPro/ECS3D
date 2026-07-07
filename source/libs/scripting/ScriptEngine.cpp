#include "ScriptEngine.h"
#include "bindings/TransformBindings.h"
#include "bindings/RigidBodyBindings.h"
#include "bindings/InputUtilsBindings.h"
#include "bindings/WorldBindings.h"
#include <ManagedHost.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
  // The gameplay bridge, published next to the executable by ecs3d_add_managed_assembly.
  const std::string kBridgeDll = "ScriptBridge.dll";
  const std::string kBridgeType = "ScriptBridge.Bridge, ScriptBridge";
}

ScriptEngine::ScriptEngine(std::shared_ptr<ManagedHost> host)
  : m_host(std::move(host))
{}

void ScriptEngine::init(const std::string& bridgeDir,
                        const std::string& scriptDir)
{
  if (!m_host || !m_host->isInitialized())
  {
    throw std::runtime_error("[ScriptEngine] ManagedHost is not initialized.");
  }

  const std::string assemblyPath = (std::filesystem::path(bridgeDir) / kBridgeDll).string();

  // Resolve the bridge entrypoints into native function pointers. Each is an [UnmanagedCallersOnly]
  // static on ScriptBridge.Bridge, so getDelegate is called without a delegate type.
  const auto resolve = [&](const char* method)
  {
    return m_host->getDelegate(assemblyPath, kBridgeType, method);
  };

  const auto initBridge = reinterpret_cast<InitBridgeFn>(resolve("init"));
  m_reload = reinterpret_cast<VoidFn>(resolve("reloadScripts"));
  m_attachScript = reinterpret_cast<AttachScriptFn>(resolve("attachScript"));
  m_detachScript = reinterpret_cast<DetachScriptFn>(resolve("detachScript"));

  m_start = reinterpret_cast<StartFn>(resolve("start"));
  m_stop = reinterpret_cast<StopFn>(resolve("stop"));

  m_fixedUpdate = reinterpret_cast<FixedUpdateFn>(resolve("fixedUpdate"));
  m_variableUpdate = reinterpret_cast<VariableUpdateFn>(resolve("variableUpdate"));

  m_getExposedFields = reinterpret_cast<GetExposedFieldsFn>(resolve("getExposedFields"));
  m_freeString = reinterpret_cast<FreeStringFn>(resolve("freeString"));

  m_getFieldFloat = reinterpret_cast<GetFieldFloatFn>(resolve("getFieldFloat"));
  m_getFieldInt = reinterpret_cast<GetFieldIntFn>(resolve("getFieldInt"));
  m_getFieldBool = reinterpret_cast<GetFieldBoolFn>(resolve("getFieldBool"));
  m_getFieldVector3 = reinterpret_cast<GetFieldVector3Fn>(resolve("getFieldVector3"));

  m_setFieldFloat = reinterpret_cast<SetFieldFloatFn>(resolve("setFieldFloat"));
  m_setFieldInt = reinterpret_cast<SetFieldIntFn>(resolve("setFieldInt"));
  m_setFieldBool = reinterpret_cast<SetFieldBoolFn>(resolve("setFieldBool"));
  m_setFieldVector3 = reinterpret_cast<SetFieldVector3Fn>(resolve("setFieldVector3"));

  registerBindings(assemblyPath, kBridgeType);

  std::cout << "[ScriptEngine] Initializing bridge, script dir: " << scriptDir << "\n";
  initBridge(scriptDir.c_str());

  m_initialized = true;
  std::cout << "[ScriptEngine] Initialized successfully.\n";
}

void ScriptEngine::registerBindings(const std::string& assemblyPath,
                                    const std::string& typeName) const
{
  // Hand the managed side the C-ABI bindings it calls back into.
  using RegisterTransformFn = void(*)(TransformBindings);
  const auto registerTransform =
    reinterpret_cast<RegisterTransformFn>(m_host->getDelegate(assemblyPath, typeName, "registerTransformBindings"));
  registerTransform(TransformBindingsProvider::getBindings());

  using RegisterRigidBodyFn = void(*)(RigidBodyBindings);
  const auto registerRigidBody =
    reinterpret_cast<RegisterRigidBodyFn>(m_host->getDelegate(assemblyPath, typeName, "registerRigidBodyBindings"));
  registerRigidBody(RigidBodyBindingsProvider::getBindings());

  using RegisterWorldFn = void(*)(WorldBindings);
  const auto registerWorld =
    reinterpret_cast<RegisterWorldFn>(m_host->getDelegate(assemblyPath, typeName, "registerWorldBindings"));
  registerWorld(WorldBindingsProvider::getBindings());

  // InputUtils reads the networked InputState (ServerApp writes it from the client's inputState
  // messages), since the headless server has no GLFW window of its own.
  using RegisterInputUtilsFn = void(*)(InputUtilsBindings);
  const auto registerInputUtils =
    reinterpret_cast<RegisterInputUtilsFn>(m_host->getDelegate(assemblyPath, typeName, "registerInputUtilsBindings"));
  registerInputUtils(InputUtilsBindingsProvider::getBindings());
}

void ScriptEngine::reloadScripts() const
{
  if (m_reload)
  {
    std::cout << "[ScriptEngine] Hot-reloading scripts...\n";
    m_reload();
    std::cout << "[ScriptEngine] Reload complete.\n";
  }
}

void ScriptEngine::attachScript(const char* uuid,
                                const char* className) const
{
  if (m_attachScript)
  {
    m_attachScript(uuid, className);
  }
}

void ScriptEngine::detachScript(const char* uuid,
                                const char* className) const
{
  if (m_detachScript)
  {
    m_detachScript(uuid, className);
  }
}

void ScriptEngine::start(const char* uuid,
                         const char* className) const
{
  if (m_start)
  {
    m_start(uuid, className);
  }
}

void ScriptEngine::stop(const char* uuid,
                        const char* className) const
{
  if (m_stop)
  {
    m_stop(uuid, className);
  }
}

void ScriptEngine::fixedUpdate(const char* uuid,
                               const char* className,
                               const float dt) const
{
  if (m_fixedUpdate)
  {
    m_fixedUpdate(uuid, className, dt);
  }
}

void ScriptEngine::variableUpdate(const char* uuid,
                                  const char* className) const
{
  if (m_variableUpdate)
  {
    m_variableUpdate(uuid, className);
  }
}

std::string ScriptEngine::getExposedFields(const char* uuid,
                                           const char* className) const
{
  if (!m_getExposedFields)
  {
    return "[]";
  }

  char* raw = m_getExposedFields(uuid, className);
  if (!raw)
  {
    return "[]";
  }

  std::string result(raw);
  if (m_freeString)
  {
    m_freeString(raw);
  }

  return result;
}

float ScriptEngine::getFieldFloat(const char* uuid,
                                  const char* className,
                                  const char* fieldName) const
{
  return m_getFieldFloat ? m_getFieldFloat(uuid, className, fieldName) : 0.0f;
}

int ScriptEngine::getFieldInt(const char* uuid,
                              const char* className,
                              const char* fieldName) const
{
  return m_getFieldInt ? m_getFieldInt(uuid, className, fieldName) : 0;
}

bool ScriptEngine::getFieldBool(const char* uuid,
                                const char* className,
                                const char* fieldName) const
{
  return m_getFieldBool ? m_getFieldBool(uuid, className, fieldName) : false;
}

void ScriptEngine::getFieldVector3(const char* uuid,
                                   const char* className,
                                   const char* fieldName,
                                   float& x, float& y, float& z) const
{
  if (m_getFieldVector3)
  {
    m_getFieldVector3(uuid, className, fieldName, &x, &y, &z);
  }
}

void ScriptEngine::setFieldFloat(const char* uuid,
                                 const char* className,
                                 const char* fieldName,
                                 const float value) const
{
  if (m_setFieldFloat)
  {
    m_setFieldFloat(uuid, className, fieldName, value);
  }
}

void ScriptEngine::setFieldInt(const char* uuid,
                               const char* className,
                               const char* fieldName,
                               const int value) const
{
  if (m_setFieldInt)
  {
    m_setFieldInt(uuid, className, fieldName, value);
  }
}

void ScriptEngine::setFieldBool(const char* uuid,
                                const char* className,
                                const char* fieldName,
                                const bool value) const
{
  if (m_setFieldBool)
  {
    m_setFieldBool(uuid, className, fieldName, value);
  }
}

void ScriptEngine::setFieldVector3(const char* uuid,
                                   const char* className,
                                   const char* fieldName,
                                   const float x, const float y, const float z) const
{
  if (m_setFieldVector3)
  {
    m_setFieldVector3(uuid, className, fieldName, x, y, z);
  }
}
