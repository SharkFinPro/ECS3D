#include "ManagedHost.h"

ManagedHost::~ManagedHost()
{
  shutdown();
}

void ManagedHost::init(const std::string& assemblyDir)
{
  // TODO: migrate the CLR-embedding half of ScriptEngine here (the part that boots the runtime,
  // TODO:   not the script calls): load hostfxr (loadLib), initialize a host context for
  // TODO:   assemblyDir, and resolve the get-function-pointer delegate. One ManagedHost owns the
  // TODO:   single CoreCLR instance per process; ECS3DNet and ECS3DScripting load their own
  // TODO:   assemblies through it.
  (void)assemblyDir;
}

void ManagedHost::shutdown()
{
  // TODO: close the host context and unload hostfxr (ScriptEngine::shutdown).
  m_initialized = false;
}

void* ManagedHost::getDelegate(const char* assemblyPath,
                               const char* typeName,
                               const char* methodName,
                               const char* delegateTypeName) const
{
  // TODO: resolve a managed method as a native function pointer via the hostfxr
  // TODO:   load-assembly-and-get-function-pointer delegate (ScriptEngine::registerBindings).
  (void)assemblyPath;
  (void)typeName;
  (void)methodName;
  (void)delegateTypeName;

  return nullptr;
}

void* ManagedHost::loadLib(const std::string& path)
{
  // TODO: platform dlopen/LoadLibrary (ScriptEngine::LoadLib).
  (void)path;

  return nullptr;
}

void* ManagedHost::getExport(void* lib,
                             const char* name)
{
  // TODO: platform dlsym/GetProcAddress (ScriptEngine::GetSymbol).
  (void)lib;
  (void)name;

  return nullptr;
}
