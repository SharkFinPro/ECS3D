#ifndef SERVERAPP_H
#define SERVERAPP_H

#include <Protocol.h>
#include <PlayerSlots.h>
#include <Lane.h>
#include "StatsSummary.h"
#include <nlohmann/json_fwd.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <uuid.h>

class ManagedHost;
class ComponentRegistry;
class AssetRegistry;
class SceneManager;
class ProjectSerializer;
class ProjectPacker;
class CollisionSystem;
class ScriptSystem;
class ObjectManager;
class SceneAsset;
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
    // Development only: an --edit server that admits any editor without a token.
    bool openEdit = false;
    // Keep ticking the scene with no one connected, instead of pausing until the next join.
    bool simulateWhenEmpty = false;
    // Seconds between stats lines in the log; 0 turns them off.
    int statsIntervalSeconds = 10;
  };

  explicit ServerApp(LaunchOptions options);

  ~ServerApp();

  [[nodiscard]] bool isActive() const;

  void run();

  // The folder that owns the project's assets and scripts; empty for a legacy file or the built-in sample.
  [[nodiscard]] const std::filesystem::path& projectRoot() const { return m_projectRoot; }

private:
  LaunchOptions m_options;

  std::filesystem::path m_projectRoot;

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
  // separate from this and keeps working either way. Null on a non-edit server. See forwardLogToEditors.
  std::shared_ptr<RemoteLogSink> m_remoteLogSink;

  profile::Lane m_lane;
  StatsCounters m_counters;
  StatsCursor m_statsCursor;
  std::chrono::steady_clock::time_point m_sampleOpenedAt;
  std::chrono::steady_clock::time_point m_lastStatsTime;
  bool m_statsIdle = false;

  std::chrono::steady_clock::time_point m_previousTime;
  const float m_fixedUpdateDt = 1.0f / 50.0f;
  float m_timeAccumulator = 0.0f;

  // Whether the last run() iteration ticked the scene, so the pause and resume are each logged once.
  bool m_simulating = true;

  // For an exitWhenEmpty server: set once the first client has connected, so isActive() only starts
  // applying the "no connections left" exit check after the spawning app has actually connected (and
  // doesn't exit during the launch -> connect window when the count is still 0).
  bool m_hasConnected = false;

  // Player<->connection binding: each connection is bound to a player slot (0, 1, ...) on join, may move
  // to another free one with possessSlot, and releases it on disconnect. inputState from a connection is
  // written into its slot; a script reads its own player's input by resolving its object's
  // PlayerController.playerSlot to that slot. Touched only on the tick thread (join / possessSlot /
  // inputState / disconnect all run there), so no locking is needed.
  PlayerSlotTable m_playerSlots;

  // The nonce each connection's join carried, so a slot change can be answered with a playerSlot.
  std::unordered_map<int32_t, uint64_t> m_joinNonces;

  // Connections refused a slot because every one is held, so the refusal logs once per connection rather
  // than on every input message.
  std::unordered_set<int32_t> m_slotlessConnections;

  // Connections whose join has been answered; a later join from one is ignored.
  std::unordered_set<int32_t> m_joinedConnections;

  // Connections whose repeat join has been logged, so a looping client logs once rather than per message.
  std::unordered_set<int32_t> m_repeatJoinWarned;

  // Bind connId to the lowest free player slot (idempotent - returns the existing slot if already bound).
  // Empty when every slot is held.
  std::optional<int32_t> assignPlayerSlot(int32_t connId);

  void handlePossessSlot(const net::Message& message, int32_t senderId);

  void sendPlayerSlot(int32_t connId);

  // Release a dropped connection's slot and clear its input.
  void handleDisconnect(int32_t connId);

  // Handle every queued client message and release the slots of dropped connections.
  void drainNetwork();

  // Logs the stats line (or the idle notice once per idle streak) when the interval has passed.
  void logStatsIfDue(std::chrono::steady_clock::time_point now);

  void fixedUpdate(float dt) const;

  // Feed this tick's collision enter/stay/exit pairs (from CollisionSystem) into the scripts. Bridges
  // sim and scripting at the app level so neither library depends on the other.
  void dispatchCollisionEvents(ObjectManager& objectManager) const;

  // Runs after the loop exits: stops live scripts and flushes the log to editors before the sockets close.
  void shutDown();

  void loadStartupProject();

  void startLoadedScene();

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

  // Hands a just-applied setSceneScriptFields edit to the running instance; any other edit does nothing.
  void pushSceneScriptFields(const SceneAsset& scene, const nlohmann::json& edit) const;

  void handleLoadProject(const net::Message& message) const;

  void finishProjectLoad() const;

  // Whether the current scene is running, the only state in which a fixed step does any work.
  [[nodiscard]] bool sceneRunning() const;

  // Scripts are live from the scene's start until its stop, paused included.
  [[nodiscard]] bool scriptsLive() const;

  void logSimulationChange(bool simulating);

  // Starts the scene's scripts, then tells its scene scripts about every player already bound.
  void startSceneScripts(SceneAsset& scene) const;

  // Drops what script code buffered for replication, for a stop where the objects it names are replaced.
  static void discardScriptBuffers();

  void notifyPlayerJoined(int32_t slot) const;

  void notifyPlayerLeft(int32_t slot) const;

  // One event per uuid to the current scene's scene scripts, spawned before destroyed. The caller flushes
  // what a handler spawned or destroyed.
  void notifyObjectEvents(const std::vector<uuids::uuid>& spawned, const std::vector<uuids::uuid>& destroyed) const;

  void startScriptsLogged(SceneAsset& scene) const;

  void stopScriptsLogged(SceneAsset& scene) const;

  void handleAddAsset(const net::Message& message) const;

  void handleRenameAsset(const net::Message& message) const;

  void handleRemoveAsset(const net::Message& message) const;

  void handleInputState(const net::Message& message, int32_t senderId);

  void handleSceneControl(const net::Message& message) const;

  void applySceneControl(net::SceneControlOp op, SceneAsset& scene, bool wasStopped) const;

  void loadScene(const std::string& sceneUUID) const;

  // Drains m_remoteLogSink (capped, so one storm-sized batch cannot dominate a send) and forwards what
  // comes out to editor connections as a serverLog message. Called once per run() loop iteration rather
  // than once per fixed tick, so a log line still gets out while the scene is stopped/paused. Does nothing
  // on a non-edit server, which has no m_remoteLogSink.
  void forwardLogToEditors() const;

  // Hot reload for an edit-mode scene that is stopped, where fixedUpdate never runs: compiles a new or
  // changed script so its fields reach the editor before Play, and re-snapshots when the reload replaced
  // the scripts. Paused scenes are left alone.
  void pollScriptChangesWhileStopped(float dt) const;

  void broadcastSnapshot() const;

  // The full project state as a snapshot message, after refreshing the script fields and script class assets
  // it carries.
  [[nodiscard]] net::Message buildSnapshot() const;

  // Answers a join: the edit status (so an editor can show a read-only cue and disable its editing UI), the
  // snapshot and the scene status, to connId alone.
  void sendJoinReply(int32_t connId) const;

  void broadcastSceneStatus() const;

  void broadcastStateDelta() const;

  // Drain the spawn/destroy a script requested this tick (buffered on BindingContext): broadcast an
  // objectSpawned/objectDestroyed per change, then actually delete the marked objects. Runs after the
  // tick's scripts, before the state delta.
  void broadcastStructuralChanges() const;
};



#endif //SERVERAPP_H
