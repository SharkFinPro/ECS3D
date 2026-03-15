#include "ScriptEngine.h"
#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
  using char_t = wchar_t;
#define STR(s) L##s
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
#define STR(s) s
  static std::string ToNative(const std::string& s) { return s; }
#endif

void* ScriptEngine::LoadLib(const std::string& path)
{
#if defined(_WIN32)
  return (void*)LoadLibraryW(ToNative(path).c_str());
#else
  return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
}

void* ScriptEngine::GetSymbol(void* lib, const char* name)
{
#if defined(_WIN32)
  return (void*)GetProcAddress(static_cast<HMODULE>(lib), name);
#else
  return dlsym(lib, name);
#endif
}

void ScriptEngine::init(const std::string& bridgeDir, const std::string& scriptDir)
{
  char_t hostfxr_path[4096] = {};
  size_t path_size = sizeof(hostfxr_path) / sizeof(char_t);

  int rc = get_hostfxr_path(hostfxr_path, &path_size, nullptr);
  if (rc != 0)
  {
    throw std::runtime_error(
      "[ScriptEngine] get_hostfxr_path() failed (code " + std::to_string(rc) + ").\n"
      "Make sure the .NET runtime >= 9 is installed: https://dotnet.microsoft.com/download"
    );
  }

#if defined(_WIN32)
  int len = WideCharToMultiByte(CP_UTF8, 0, hostfxr_path, -1, nullptr, 0, nullptr, nullptr);
  std::string hostfxr_str(len, 0);
  WideCharToMultiByte(CP_UTF8, 0, hostfxr_path, -1, hostfxr_str.data(), len, nullptr, nullptr);
#else
  std::string hostfxr_str(hostfxr_path);
#endif

  std::cout << "[ScriptEngine] Loading hostfxr: " << hostfxr_str << "\n";
  m_hostfxrLib = LoadLib(hostfxr_str);
  if (!m_hostfxrLib)
  {
    throw std::runtime_error("[ScriptEngine] Failed to load hostfxr: " + hostfxr_str);
  }

  auto init_fn = (hostfxr_initialize_for_runtime_config_fn)
    GetSymbol(m_hostfxrLib, "hostfxr_initialize_for_runtime_config");

  if (!init_fn)
  {
    throw std::runtime_error("[ScriptEngine] Could not find hostfxr_initialize_for_runtime_config");
  }

  const std::filesystem::path runtimeConfig =
    std::filesystem::path(bridgeDir) / "ScriptBridge.runtimeconfig.json";

  if (!std::filesystem::exists(runtimeConfig))
  {
    throw std::runtime_error(
      "[ScriptEngine] runtimeconfig not found: " + runtimeConfig.string() + "\n"
      "Did 'dotnet publish' succeed for scripts/ScriptBridge?"
    );
  }

  rc = init_fn(ToNative(runtimeConfig.string()).c_str(), nullptr, &m_hostContext);
  if (rc != 0)
  {
    throw std::runtime_error(
      "[ScriptEngine] hostfxr_initialize_for_runtime_config() failed (code "
      + std::to_string(rc) + ")"
    );
  }

  const auto get_delegate = (hostfxr_get_runtime_delegate_fn)
    GetSymbol(m_hostfxrLib, "hostfxr_get_runtime_delegate");

  if (!get_delegate)
  {
    throw std::runtime_error("[ScriptEngine] Could not find hostfxr_get_runtime_delegate");
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
      "[ScriptEngine] Failed to get load_assembly delegate (code " + std::to_string(rc) + ")"
    );
  }

  const std::filesystem::path dllPath = std::filesystem::path(bridgeDir) / "ScriptBridge.dll";
  if (!std::filesystem::exists(dllPath))
  {
    throw std::runtime_error("[ScriptEngine] ScriptBridge.dll not found: " + dllPath.string());
  }

  const auto dll_native = ToNative(dllPath.string());
  const auto bridgeType_native = ToNative("ScriptBridge.Bridge, ScriptBridge");

  auto loadFn = [&](const char* method, void** out)
  {
    const auto method_native = ToNative(std::string(method));
    const int r = load_assembly(
      dll_native.c_str(),
      bridgeType_native.c_str(),
      method_native.c_str(),
      UNMANAGEDCALLERSONLY_METHOD,
      nullptr,
      out
    );

    if (r != 0 || !*out)
    {
      throw std::runtime_error(
        std::string("[ScriptEngine] Failed to bind Bridge.") + method
        + " (code " + std::to_string(r) + ")"
      );
    }
  };

  InitBridgeFn initBridgeFn = nullptr;
  loadFn("init", reinterpret_cast<void**>(&initBridgeFn));
  loadFn("reloadScripts", reinterpret_cast<void**>(&m_reload));
  loadFn("fixedUpdate", reinterpret_cast<void**>(&m_fixedUpdate));
  loadFn("variableUpdate", reinterpret_cast<void**>(&m_variableUpdate));

  std::cout << "[ScriptEngine] Initializing bridge, script dir: " << scriptDir << "\n";
  initBridgeFn(scriptDir.c_str());

  m_initialized = true;
  std::cout << "[ScriptEngine] Initialized successfully.\n";
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

void ScriptEngine::shutdown()
{
  if (!m_initialized)
  {
    return;
  }

  if (m_hostContext)
  {
    auto close_fn = (hostfxr_close_fn)GetSymbol(m_hostfxrLib, "hostfxr_close");
    if (close_fn)
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
  m_initialized = false;

  std::cout << "[ScriptEngine] Shutdown complete.\n";
}

void ScriptEngine::fixedUpdate(const float dt) const
{
  if (m_fixedUpdate)
  {
    m_fixedUpdate(dt);
  }
}

void ScriptEngine::variableUpdate() const
{
  if (m_variableUpdate)
  {
    m_variableUpdate();
  }
}

ScriptEngine::~ScriptEngine()
{
  shutdown();
}
