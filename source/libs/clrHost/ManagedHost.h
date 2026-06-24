#ifndef MANAGEDHOST_H
#define MANAGEDHOST_H

#include <string>

class ManagedHost {
public:
  ManagedHost() = default;
  ManagedHost(const ManagedHost&) = delete;
  ManagedHost& operator=(const ManagedHost&) = delete;

  ~ManagedHost();

  void init(const std::string& assemblyDir);

  void shutdown();

  [[nodiscard]] void* getDelegate(const char* assemblyPath,
                                  const char* typeName,
                                  const char* methodName,
                                  const char* delegateTypeName) const;

private:
  void* m_hostfxrLib = nullptr;
  void* m_hostContext = nullptr;
  bool m_initialized = false;

  static void* loadLib(const std::string& path);

  static void* getExport(void* lib,
                         const char* name);
};



#endif //MANAGEDHOST_H
