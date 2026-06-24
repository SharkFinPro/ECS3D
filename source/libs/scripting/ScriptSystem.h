#ifndef SCRIPTSYSTEM_H
#define SCRIPTSYSTEM_H

#include <memory>

class ManagedHost;
class ObjectManager;

class ScriptSystem {
public:
  explicit ScriptSystem(std::shared_ptr<ManagedHost> host);

  void start(ObjectManager& objectManager);

  void stop(ObjectManager& objectManager);

  void fixedUpdate(ObjectManager& objectManager, float dt);

private:
  std::shared_ptr<ManagedHost> m_host;

  // TODO: load the ScriptBridge assembly through m_host and resolve its entrypoints (the script-call
  // TODO:   half of ScriptEngine), plus migrate ScriptManager (attach tracking, hot-reload snapshot,
  // TODO:   field cache). Gameplay only ever runs here, on the server.
};



#endif //SCRIPTSYSTEM_H
