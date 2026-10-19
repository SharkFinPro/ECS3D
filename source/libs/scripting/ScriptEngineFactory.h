#ifndef SCRIPTENGINEFACTORY_H
#define SCRIPTENGINEFACTORY_H

#include "ScriptRuntime.h"
#include <functional>
#include <filesystem>
#include <memory>
#include <vector>

class ManagedHost;

// Builds and initializes a ScriptEngine over the host: loads ScriptBridge and compiles the scripts in
// scriptDirectories together.
[[nodiscard]] std::function<std::unique_ptr<ScriptRuntime>()> makeScriptEngineFactory(
  std::shared_ptr<ManagedHost> host,
  std::vector<std::filesystem::path> scriptDirectories);

#endif //SCRIPTENGINEFACTORY_H
