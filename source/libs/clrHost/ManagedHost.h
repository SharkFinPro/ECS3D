#ifndef MANAGEDHOST_H
#define MANAGEDHOST_H

#include <string>

// The single CoreCLR instance for the process. It boots the runtime from a runtimeconfig and hands
// out managed methods as native function pointers; ECS3DNet and ECS3DScripting each load their own
// assemblies through it (the transport socket and the gameplay bridge respectively).
class ManagedHost {
public:
  ManagedHost() = default;
  ManagedHost(const ManagedHost&) = delete;
  ManagedHost& operator=(const ManagedHost&) = delete;

  ~ManagedHost();

  // assemblyDir holds a *.runtimeconfig.json to bootstrap the runtime from (the net transport's, since
  // every app links ECS3DNet). Assemblies are then loaded by path through getDelegate.
  void init(const std::string& assemblyDir);

  void shutdown();

  // Resolve a managed static method as a native function pointer. delegateTypeName may be nullptr for
  // an [UnmanagedCallersOnly] method (the common case); otherwise it names the managed delegate type.
  [[nodiscard]] void* getDelegate(const std::string& assemblyPath,
                                  const std::string& typeName,
                                  const std::string& methodName,
                                  const std::string& delegateTypeName = "") const;

  [[nodiscard]] bool isInitialized() const { return m_initialized; }

private:
  void* m_hostfxrLib = nullptr;
  void* m_hostContext = nullptr;
  void* m_loadAssembly = nullptr;
  bool m_initialized = false;

  static void* loadLib(const std::string& path);

  static void* getExport(void* lib,
                         const char* name);
};



#endif //MANAGEDHOST_H
