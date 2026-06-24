#ifndef SCRIPTSYSTEM_H
#define SCRIPTSYSTEM_H

#include <memory>

class ManagedHost;
class SceneAsset;

class ScriptSystem {
public:
  explicit ScriptSystem(std::shared_ptr<ManagedHost> host);

  void start(const std::shared_ptr<SceneAsset>& scene);

  void stop(const std::shared_ptr<SceneAsset>& scene);

  void fixedUpdate(const std::shared_ptr<SceneAsset>& scene, float dt);

private:
  std::shared_ptr<ManagedHost> m_host;

  // TODO: load the ScriptBridge assembly through m_host and resolve its entrypoints (the script-
  // TODO:   call delegates that used to be the function-pointer half of ScriptEngine). Drive every
  // TODO:   Script component's managed fixedUpdate. Gameplay only ever runs here, on the server.
};



#endif //SCRIPTSYSTEM_H
