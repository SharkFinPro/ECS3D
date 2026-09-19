#ifndef SERVERAPP_H
#define SERVERAPP_H

#include <Protocol.h>
#include <nlohmann/json_fwd.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

class ManagedHost;
class ComponentRegistry;
class AssetRegistry;
class SceneManager;
class ProjectSerializer;
class ProjectPacker;
class CollisionSystem;
class ScriptSystem;
class ObjectManager;
class RemoteLogSink;

namespace net {
  class NetServer;
  class Message;
}

// The authoritative server. It owns the simulation and is the only thing that links ECS3DSim + ECS3DScripting.
class ServerApp final {
public:
  struct LaunchOptions {
    std::string project;
    int port = net::defaultPort;
    bool editMode = false;
    // When set (an editor/client-spawned local server), the server exits once its last connection drops
    // instead of running until killed like a dedicated server.
    bool exitWhenEmpty = false;
    std::string authToken;
  };

  explicit ServerApp(LaunchOptions options);

  ~ServerApp();

  [[nodiscard]] bool isActive() const;

  void run();

private:
  LaunchOptions m_options;

  std::shared_ptr<ManagedHost> m_host;
  std::shared_ptr<net::NetServer> m_netServer;

  std::shared_ptr<ComponentRegistry> m_componentRegistry;
  std::shared_ptr<AssetRegistry> m_assetRegistry;
  std::shared_ptr<SceneManager> m_sceneManager;
  std::shared_ptr<ProjectSerializer> m_projectSerializer;
  std::shared_ptr<ProjectPacker> m_projectPacker;

  std::shared_ptr<CollisionSystem> m_collisionSystem;
  std::shared_ptr<ScriptSystem> m_scriptSystem;

  // Registered with Log so the server's own log (including script output, which already reaches Log via
  // LogBindings) can be forwarded to editor connections - the local console window some launches show is
  // separate from this and keeps working either way. See forwardLogToEditors.
  std::shared_ptr<RemoteLogSink> m_remoteLogSink;

  std::chrono::steady_clock::time_point m_previousTime;
  const float m_fixedUpdateDt = 1.0f / 50.0f;
  float m_timeAccumulator = 0.0f;

  // For an exitWhenEmpty server: set once the first client has connected, so isActive() only starts
  // applying the "no connections left" exit check after the spawning app has actually connected (and
  // doesn't exit during the launch -> connect window when the count is still 0).
  bool m_hasConnected = false;

  // Player<->connection binding: each connection is bound to a player slot (0, 1, ...) on join
  // and released on disconnect. inputState from a connection is written into its slot; a script reads its
  // own player's input by resolving its object's PlayerController.playerSlot to that slot. Touched only on
  // the tick thread (join / inputState / disconnect all run there), so no locking is needed.
  std::unordered_map<int32_t, int32_t> m_connectionSlots;

  // Bind connId to the lowest free player slot (idempotent - returns the existing slot if already bound).
  int32_t assignPlayerSlot(int32_t connId);

  // Release a dropped connection's slot and clear its input.
  void handleDisconnect(int32_t connId);

  void fixedUpdate(float dt) const;

  // Feed this tick's collision enter/stay/exit pairs (from CollisionSystem) into the scripts. Bridges
  // sim and scripting at the app level so neither library depends on the other.
  void dispatchCollisionEvents(ObjectManager& objectManager) const;

  // senderId is the stable connection id of the message's origin (from NetServer::poll), so per-client
  // messages like inputState land in the right slot.
  void handleClientMessage(const net::Message& message, int32_t senderId);

  // A non-edit server is read-only, and on an edit-mode server only the authorized editor connection may
  // send mutations.
  [[nodiscard]] bool isMessageAuthorized(const net::Message& message, int32_t senderId) const;

  void handleEditorMessage(const net::Message& message) const;

  void handleJoin(const net::Message& message, int32_t senderId);

  void handleEditComponent(const net::Message& message) const;

  void handleSceneEdit(const net::Message& message) const;

  void handleLoadProject(const net::Message& message) const;

  void finishProjectLoad(bool wasRunning) const;

  void startScriptsLogged(ObjectManager& objectManager) const;

  void stopScriptsLogged(ObjectManager& objectManager) const;

  void handleAddAsset(const net::Message& message) const;

  void handleRenameAsset(const net::Message& message) const;

  void handleRemoveAsset(const net::Message& message) const;

  void handleInputState(const net::Message& message, int32_t senderId);

  void handleSceneControl(const net::Message& message) const;

  void applySceneControl(net::SceneControlOp op, ObjectManager& objectManager, bool wasStopped) const;

  void loadScene(const std::string& sceneUUID) const;

  // Drains m_remoteLogSink (capped, so one storm-sized batch cannot dominate a send) and forwards what
  // comes out to editor connections as a serverLog message. Called once per run() loop iteration rather
  // than once per fixed tick, so a log line still gets out while the scene is stopped/paused.
  void forwardLogToEditors() const;

  void broadcastSnapshot() const;

  // Tells clients whether this server accepts edits (true only for an edit-mode server), so an editor
  // can show a read-only cue and disable its editing UI instead of looking broken/blank.
  void broadcastEditStatus() const;

  void broadcastSceneStatus() const;

  void broadcastStateDelta() const;

  // Drain the spawn/destroy a script requested this tick (buffered on BindingContext): broadcast an
  // objectSpawned/objectDestroyed per change, then actually delete the marked objects. Runs after the
  // tick's scripts, before the state delta.
  void broadcastStructuralChanges() const;
};



#endif //SERVERAPP_H
