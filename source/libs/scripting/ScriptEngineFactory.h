#ifndef SCRIPTENGINEFACTORY_H
#define SCRIPTENGINEFACTORY_H

#include "ScriptRuntime.h"
#include <functional>
#include <memory>

class ManagedHost;

// Builds and initializes a ScriptEngine over the host: loads ScriptBridge and compiles the user scripts.
[[nodiscard]] std::function<std::unique_ptr<ScriptRuntime>()> makeScriptEngineFactory(std::shared_ptr<ManagedHost> host);

#endif //SCRIPTENGINEFACTORY_H
