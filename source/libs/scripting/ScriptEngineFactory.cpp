#include "ScriptEngineFactory.h"
#include "ScriptEngine.h"
#include <string>
#include <utility>

namespace {
  // Published next to the executable by ecs3d_add_managed_assembly. Relative paths assume CWD = exe
  // dir, matching the rest of the engine's asset loading.
  const std::string kScriptBridgeDir = "scripts/ScriptBridge";
}

std::function<std::unique_ptr<ScriptRuntime>()> makeScriptEngineFactory(std::shared_ptr<ManagedHost> host)
{
  return [host = std::move(host)]() -> std::unique_ptr<ScriptRuntime>
  {
    auto engine = std::make_unique<ScriptEngine>(host);
    engine->init(kScriptBridgeDir, scripting::defaultUserScriptsDir);

    return engine;
  };
}
