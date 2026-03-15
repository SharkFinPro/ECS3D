#pragma once

#include <string>

class ScriptEngine
{
public:
    ScriptEngine() = default;
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&)            = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    void Init(const std::string& bridgeDir, const std::string& scriptDir);
    void Update(float dt, int& counter) const;
    void ReloadScripts() const;
    void Shutdown();

private:
    static void* LoadLib(const std::string& path);
    static void* GetSymbol(void* lib, const char* name);

    using UpdateAllFn  = void(*)(float, int*);
    using VoidFn       = void(*)();
    using InitBridgeFn = void(*)(const char*);

    void* m_hostfxrLib  = nullptr;
    void* m_hostContext = nullptr;
    bool  m_initialized = false;

    UpdateAllFn m_updateAll = nullptr;
    VoidFn      m_reload    = nullptr;
};
