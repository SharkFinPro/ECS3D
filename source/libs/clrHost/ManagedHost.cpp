#include "ManagedHost.h"
#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
  using char_t = wchar_t;
  static std::wstring ToNative(const std::string& s)
  {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
  }
#else
#include <dlfcn.h>
  using char_t = char;
  static std::string ToNative(const std::string& s) { return s; }
#endif

ManagedHost::~ManagedHost()
{
  shutdown();
}

void ManagedHost::init(const std::string& assemblyDir)
{
  // Boot the runtime from the first runtimeconfig found in assemblyDir. The host context is generic;
  // individual assemblies are loaded later by path via getDelegate.
  char_t hostfxr_path[4096] = {};
  size_t path_size = sizeof(hostfxr_path) / sizeof(char_t);

  int rc = get_hostfxr_path(hostfxr_path, &path_size, nullptr);
  if (rc != 0)
  {
    throw std::runtime_error(
      "[ManagedHost] get_hostfxr_path() failed (code " + std::to_string(rc) + ").\n"
      "Make sure the .NET runtime >= 9 is installed: https://dotnet.microsoft.com/download"
    );
  }

#if defined(_WIN32)
  const int len = WideCharToMultiByte(CP_UTF8, 0, hostfxr_path, -1, nullptr, 0, nullptr, nullptr);
  std::string hostfxr_str(len, 0);
  WideCharToMultiByte(CP_UTF8, 0, hostfxr_path, -1, hostfxr_str.data(), len, nullptr, nullptr);
#else
  std::string hostfxr_str(hostfxr_path);
#endif

  std::cout << "[ManagedHost] Loading hostfxr: " << hostfxr_str << "\n";
  m_hostfxrLib = loadLib(hostfxr_str);
  if (!m_hostfxrLib)
  {
    throw std::runtime_error("[ManagedHost] Failed to load hostfxr: " + hostfxr_str);
  }

  const auto init_fn = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
    getExport(m_hostfxrLib, "hostfxr_initialize_for_runtime_config"));

  if (!init_fn)
  {
    throw std::runtime_error("[ManagedHost] Could not find hostfxr_initialize_for_runtime_config");
  }

  std::filesystem::path runtimeConfig;
  if (std::filesystem::exists(assemblyDir))
  {
    for (const auto& entry : std::filesystem::directory_iterator(assemblyDir))
    {
      if (entry.path().extension() == ".json"
          && entry.path().filename().string().find(".runtimeconfig.") != std::string::npos)
      {
        runtimeConfig = entry.path();
        break;
      }
    }
  }

  if (runtimeConfig.empty())
  {
    throw std::runtime_error(
      "[ManagedHost] No *.runtimeconfig.json found in: " + assemblyDir + "\n"
      "Did the C# assemblies publish next to the executable?"
    );
  }

  rc = init_fn(ToNative(runtimeConfig.string()).c_str(), nullptr, &m_hostContext);
  if (rc != 0)
  {
    throw std::runtime_error(
      "[ManagedHost] hostfxr_initialize_for_runtime_config() failed (code "
      + std::to_string(rc) + ")"
    );
  }

  const auto get_delegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
    getExport(m_hostfxrLib, "hostfxr_get_runtime_delegate"));

  if (!get_delegate)
  {
    throw std::runtime_error("[ManagedHost] Could not find hostfxr_get_runtime_delegate");
  }

  load_assembly_and_get_function_pointer_fn load_assembly = nullptr;
  rc = get_delegate(
    m_hostContext,
    hdt_load_assembly_and_get_function_pointer,
    reinterpret_cast<void**>(&load_assembly)
  );

  if (rc != 0 || !load_assembly)
  {
    throw std::runtime_error(
      "[ManagedHost] Failed to get load_assembly delegate (code " + std::to_string(rc) + ")"
    );
  }

  m_loadAssembly = reinterpret_cast<void*>(load_assembly);
  m_initialized = true;

  std::cout << "[ManagedHost] Runtime booted from: " << runtimeConfig.string() << "\n";
}

void ManagedHost::shutdown()
{
  if (!m_initialized)
  {
    return;
  }

  if (m_hostContext)
  {
    if (const auto close_fn = reinterpret_cast<hostfxr_close_fn>(getExport(m_hostfxrLib, "hostfxr_close")))
    {
      close_fn(m_hostContext);
    }

    m_hostContext = nullptr;
  }

#if defined(_WIN32)
  if (m_hostfxrLib)
  {
    FreeLibrary(static_cast<HMODULE>(m_hostfxrLib));
  }
#else
  if (m_hostfxrLib)
  {
    dlclose(m_hostfxrLib);
  }
#endif

  m_hostfxrLib = nullptr;
  m_loadAssembly = nullptr;
  m_initialized = false;

  std::cout << "[ManagedHost] Shutdown complete.\n";
}

void* ManagedHost::getDelegate(const std::string& assemblyPath,
                               const std::string& typeName,
                               const std::string& methodName,
                               const std::string& delegateTypeName) const
{
  if (!m_initialized || !m_loadAssembly)
  {
    return nullptr;
  }

  if (!std::filesystem::exists(assemblyPath))
  {
    throw std::runtime_error("[ManagedHost] Assembly not found: " + assemblyPath);
  }

  const auto load_assembly =
    reinterpret_cast<load_assembly_and_get_function_pointer_fn>(m_loadAssembly);

  const auto assembly_native = ToNative(assemblyPath);
  const auto type_native = ToNative(typeName);
  const auto method_native = ToNative(methodName);
  const auto delegate_native = delegateTypeName.empty() ? std::basic_string<char_t>() : ToNative(delegateTypeName);

  void* fnPtr = nullptr;
  const int rc = load_assembly(
    assembly_native.c_str(),
    type_native.c_str(),
    method_native.c_str(),
    delegateTypeName.empty() ? UNMANAGEDCALLERSONLY_METHOD : delegate_native.c_str(),
    nullptr,
    &fnPtr
  );

  if (rc != 0 || !fnPtr)
  {
    throw std::runtime_error(
      "[ManagedHost] Failed to bind " + typeName + "." + methodName
      + " (code " + std::to_string(rc) + ")"
    );
  }

  return fnPtr;
}

void* ManagedHost::loadLib(const std::string& path)
{
#if defined(_WIN32)
  return static_cast<void*>(LoadLibraryW(ToNative(path).c_str()));
#else
  return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
}

void* ManagedHost::getExport(void* lib,
                             const char* name)
{
#if defined(_WIN32)
  return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
  return dlsym(lib, name);
#endif
}
