#ifndef SERVERAPP_H
#define SERVERAPP_H

#include <memory>
#include <string>

class ManagedHost;
class ComponentRegistry;
class SceneManager;
class PhysicsSystem;
class CollisionSystem;
class ScriptSystem;

namespace net {
  class NetServer;
}

class ServerApp {
public:
  struct LaunchOptions {
    std::string project;
    int port = 0;
    bool editMode = false;
    std::string authToken;
  };

  explicit ServerApp(LaunchOptions options);

  [[nodiscard]] bool isActive() const;

  void run();

private:
  LaunchOptions m_options;

  std::shared_ptr<ManagedHost> m_host;
  std::shared_ptr<net::NetServer> m_netServer;

  std::shared_ptr<ComponentRegistry> m_componentRegistry;
  std::shared_ptr<SceneManager> m_sceneManager;

  std::shared_ptr<PhysicsSystem> m_physicsSystem;
  std::shared_ptr<CollisionSystem> m_collisionSystem;
  std::shared_ptr<ScriptSystem> m_scriptSystem;

  const float m_fixedUpdateDt = 1.0f / 50.0f;
  float m_timeAccumulator = 0.0f;

  void fixedUpdate(float dt);
};



#endif //SERVERAPP_H
