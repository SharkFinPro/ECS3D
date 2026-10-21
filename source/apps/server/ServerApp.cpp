#include "ServerApp.h"
#include "DefaultProject.h"
#include "ServerPolicy.h"
#include <ComponentRegistry.h>
#include <ComponentRegistration.h>
#include <ProjectPaths.h>
#include <ProjectSerializer.h>
#include <ProjectPacker.h>
#include <assets/AssetRegistry.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/Object.h>
#include <objects/components/Component.h>
#include <PhysicsSystem.h>
#include <FixedTimestep.h>
#include <CollisionSystem.h>
#include <queries/SceneQueries.h>
#include <ScriptSystem.h>
#include <ScriptDirectories.h>
#include <ScriptEngineFactory.h>
#include <bindings/InputState.h>
#include <bindings/BindingContext.h>
#include <NetServer.h>
#include <ManagedHost.h>
#include <Log.h>
#include <RemoteLogSink.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>

ServerApp::ServerApp(LaunchOptions options)
  : m_options(std::move(options)),
    m_host(std::make_shared<ManagedHost>()),
    m_componentRegistry(std::make_shared<ComponentRegistry>()),
    m_assetRegistry(std::make_shared<AssetRegistry>()),
    m_sceneManager(std::make_shared<SceneManager>()),
    m_previousTime(std::chrono::steady_clock::now())
{
  // Boot the CLR from the net transport's runtimeconfig (ECS3DNet is linked by every app). The
  // ScriptBridge assembly is loaded on top of the same runtime by ScriptSystem.
  m_host->init("net/Transport");

  registerDataComponents(*m_componentRegistry);

  m_projectSerializer = std::make_shared<ProjectSerializer>(m_assetRegistry.get(), m_sceneManager.get(), m_componentRegistry);
  m_projectPacker = std::make_shared<ProjectPacker>(m_assetRegistry.get(), m_sceneManager.get(), m_componentRegistry);
  m_collisionSystem = std::make_shared<CollisionSystem>();
  m_netServer = std::make_shared<net::NetServer>(m_host);

  // Feeds forwardLogToEditors: the server is headless, so this is the only way a connected editor - local
  // or remote - sees anything the server (or a script running on it, via LogBindings) logs. Edit-mode
  // servers only; see forwardsLogToEditors.
  if (forwardsLogToEditors(m_options.editMode))
  {
    m_remoteLogSink = std::make_shared<RemoteLogSink>();
    Log::addSink(m_remoteLogSink);
  }

  // Scene queries live in sim, which scripting can't link; inject them into BindingContext so the World
  // raycast/overlapSphere bindings can call them.
  BindingContext::setRaycast(&SceneQueries::raycast);
  BindingContext::setOverlapSphere(&SceneQueries::overlapSphere);

  // The World spawnPrefab binding resolves a prefab uuid to its body through the registry. Injected once:
  // loadProject reassigns the registry's contents, never the object.
  BindingContext::setAssetRegistry(m_assetRegistry.get());

  // A restarted server must read as a different project load to the editors attached to it.
  std::mt19937 rng{ std::random_device{}() };
  m_projectPacker->setProjectLoadId(uuids::uuid_random_generator{ rng }());

  loadStartupProject();

  // Built after the project is located, since the project's own scripts compile with the built-in ones.
  const auto scriptDirs = scripting::scriptDirectories(scripting::defaultUserScriptsDir, m_projectRoot);
  m_scriptSystem = std::make_shared<ScriptSystem>(makeScriptEngineFactory(m_host, scriptDirs), scriptDirs);

  // Compiling the user scripts is the slowest part of boot; doing it before listening keeps it off the
  // first editor's join, whose snapshot would otherwise wait on it.
  try
  {
    m_scriptSystem->warmUp();
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, std::string("Script runtime failed to initialize at startup: ") + e.what());
  }

  m_netServer->start(m_options.port, m_options.editMode, m_options.authToken);

  startLoadedScene();
}

void ServerApp::loadStartupProject()
{
  if (m_options.project.empty())
  {
    // No project file requested: run the built-in sample (Welcome and the demo scenes). It's generated in
    // code because the procedural scenes can't be a static file on disk.
    m_projectSerializer->deserialize(buildDefaultProject());
  }
  else
  {
    const auto location = locateProject(m_options.project);
    m_projectRoot = location.root;

    if (!m_projectSerializer->load(location.projectFile.string()) || !m_sceneManager->getCurrentScene())
    {
      Log::error(LogCategory::server, "No scene loaded from project '" + location.projectFile.string()
        + "' - the server will run but simulate nothing. Check the project path and working directory.");
    }
  }
}

void ServerApp::startLoadedScene()
{
  // An edit server waits for the editor's play; a play server has no one to press it.
  const bool autoStart = !m_options.editMode;
  if (autoStart)
  {
    m_sceneManager->startScene();
  }

  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    if (autoStart)
    {
      try
      {
        startSceneScripts(*scene);
      }
      catch (const std::exception& e)
      {
        Log::error(LogCategory::server, e.what());
      }
    }

    // The server is headless (no window), so announce that it's up - otherwise a running server looks
    // like it never started.
    Log::info(LogCategory::server, std::string(autoStart ? "Running" : "Loaded") + " scene '" + scene->getName()
      + "' (" + std::to_string(scene->getObjectManager()->getAllObjects().size()) + " objects) on port "
      + std::to_string(m_options.port) + ".");
  }
}

ServerApp::~ServerApp()
{
  if (m_netServer)
  {
    m_netServer->stop();
  }

  if (m_host)
  {
    m_host->shutdown();
  }
}

bool ServerApp::isActive() const
{
  // A dedicated server runs until killed.
  if (!m_options.exitWhenEmpty)
  {
    return true;
  }

  // An ephemeral (editor/client-spawned) server exits once its last connection drops - but only after the
  // first join (m_hasConnected), so it survives the launch -> connect window when the count is still 0.
  return !m_hasConnected || m_netServer->connectionCount() > 0;
}

void ServerApp::run()
{
  while (isActive())
  {
    net::Message message;
    int32_t senderId = 0;
    while (m_netServer->poll(message, senderId))
    {
      // A bad/malicious message (or a script-bridge hiccup while building a snapshot) must not take the
      // whole server down - that would look like "client connected, then nothing".
      try
      {
        handleClientMessage(message, senderId);
      }
      catch (const std::exception& e)
      {
        Log::error(LogCategory::server, std::string("Failed to handle client message: ") + e.what());
      }
    }

    // Release the player slot of any connection that dropped, so a departed player's input doesn't linger
    // and its slot is free for the next joiner.
    for (const auto connId : m_netServer->takeDisconnected())
    {
      try
      {
        handleDisconnect(connId);
      }
      catch (const std::exception& e)
      {
        Log::error(LogCategory::server, std::string("Failed to handle disconnect: ") + e.what());
      }
    }

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - m_previousTime).count();
    m_previousTime = now;

    // Caps how many ticks one real frame will replay. A stall (GC pause, breakpoint, OS scheduling
    // hiccup) banks real time in m_timeAccumulator; without a cap the next frame(s) would replay all of
    // it as a burst of full-speed ticks - e.g. a player holding a movement key would see it launched
    // across the whole stall's worth of distance the instant the stall clears. FixedTimestep::advance
    // drops anything past the cap instead of carrying it forward, so a stall of any length costs at most
    // this many ticks' worth of movement.
    constexpr int maxFixedStepsPerFrame = 3;

    const auto plan = FixedTimestep::advance(m_timeAccumulator, dt, m_fixedUpdateDt, maxFixedStepsPerFrame);
    m_timeAccumulator = plan.remainingAccumulator;

    const bool ticked = plan.steps > 0;
    for (int step = 0; step < plan.steps; ++step)
    {
      fixedUpdate(m_fixedUpdateDt);

      // The tick's scripts have read this tick's mouse motion + key edges; zero the accumulated delta/scroll
      // and snapshot the current keys as "last tick's", so a still mouse reads zero and
      // wasPressed/ReleasedThisTick reflect only genuinely new changes.
      InputState::clearMouseDeltas();
      InputState::commitInputEdges();
    }

    // Only stream a delta when the sim actually advanced. The server is headless (no vsync), so without
    // this guard the unbounded loop would flood every client with deltas and stall their drain loops.
    if (ticked)
    {
      // Replicate any runtime spawn/destroy the tick's scripts requested before the delta, so a client
      // has the object (or has dropped it) by the time the delta for this tick references it.
      broadcastStructuralChanges();

      broadcastStateDelta();
    }

    // Every loop iteration rather than gated on `ticked`: a log line (e.g. a startup error) can happen
    // while the scene is stopped or paused, and an editor watching the Console shouldn't have to wait for
    // the sim to advance to see it.
    forwardLogToEditors();

    pollScriptChangesWhileStopped(dt);

    // Don't busy-spin a core between ticks.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

bool ServerApp::scriptsLive() const
{
  return m_sceneManager->getCurrentScene() && m_sceneManager->getSceneStatus() != SceneStatus::stopped;
}

void ServerApp::pollScriptChangesWhileStopped(const float dt) const
{
  const auto scene = m_sceneManager->getCurrentScene();
  if (!shouldPollScriptsWhileStopped(m_options.editMode, scene != nullptr, m_sceneManager->getSceneStatus()))
  {
    return;
  }

  try
  {
    if (m_scriptSystem->pollScriptChanges(*scene, dt))
    {
      broadcastSnapshot();
    }
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, std::string("Script change check failed: ") + e.what());
  }
}

void ServerApp::startSceneScripts(SceneAsset& scene) const
{
  m_scriptSystem->start(scene);

  for (const auto slot : m_playerSlots.heldSlots())
  {
    m_scriptSystem->dispatchSceneEvent(scene, SceneEvent::playerJoined, nullptr, slot);
  }

  broadcastStructuralChanges();
}

void ServerApp::discardScriptBuffers()
{
  // Whatever an unloading handler (or a stopping script) buffered names objects the stop rebuild is about to
  // replace, so replicating it next tick would describe objects that no longer exist.
  [[maybe_unused]] const auto spawned = BindingContext::takeSpawned();
  [[maybe_unused]] const auto destroyed = BindingContext::takeDestroyed();
  [[maybe_unused]] const auto componentEdits = BindingContext::takeComponentEdits();
  [[maybe_unused]] const auto componentChanges = BindingContext::takeStructuralComponentChanges();
}

void ServerApp::notifyPlayerJoined(const int32_t slot) const
{
  if (scriptsLive())
  {
    m_scriptSystem->dispatchSceneEvent(*m_sceneManager->getCurrentScene(), SceneEvent::playerJoined, nullptr, slot);
    broadcastStructuralChanges();
  }
}

void ServerApp::notifyPlayerLeft(const int32_t slot) const
{
  if (scriptsLive())
  {
    m_scriptSystem->dispatchSceneEvent(*m_sceneManager->getCurrentScene(), SceneEvent::playerLeft, nullptr, slot);
    broadcastStructuralChanges();
  }
}

// Does not flush: the caller is either broadcastStructuralChanges, where anything a handler buffers is
// replicated by the next call, or an edit path that flushes itself afterwards.
void ServerApp::notifyObjectEvents(const std::vector<uuids::uuid>& spawned,
                                   const std::vector<uuids::uuid>& destroyed) const
{
  if (!scriptsLive())
  {
    return;
  }

  const auto scene = m_sceneManager->getCurrentScene();
  for (const auto& uuid : spawned)
  {
    m_scriptSystem->dispatchSceneEvent(*scene, SceneEvent::objectSpawned, &uuid);
  }

  for (const auto& uuid : destroyed)
  {
    m_scriptSystem->dispatchSceneEvent(*scene, SceneEvent::objectDestroyed, &uuid);
  }
}

void ServerApp::fixedUpdate(const float dt) const
{
  const auto scene = m_sceneManager->getCurrentScene();
  if (!scene || m_sceneManager->getSceneStatus() != SceneStatus::running)
  {
    return;
  }

  // The number crunching, in order: scripts read input (variableUpdate) then queue forces, physics
  // integrates, collisions resolve. variableUpdate runs before fixedUpdate so input-driven force is
  // applied the same tick (the server has no render frame to drive it separately).
  try
  {
    auto& objectManager = *scene->getObjectManager();

    m_scriptSystem->variableUpdate(*scene);
    m_scriptSystem->fixedUpdate(*scene, dt);
    PhysicsSystem::fixedUpdate(objectManager, dt);
    m_collisionSystem->fixedUpdate(objectManager, dt);

    // Contact events for this tick: hand CollisionSystem's diffed pair lists to the scripts. Done here
    // in the app (not as a library call) so sim stays independent of scripting - the collision system
    // produces plain uuid pairs and ScriptSystem consumes them.
    dispatchCollisionEvents(objectManager);
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, e.what());
  }
}

void ServerApp::dispatchCollisionEvents(ObjectManager& objectManager) const
{
  // A collision pair notifies both of its objects (each learns of the other); ScriptSystem expands the
  // pair into both directions and skips any object that was destroyed. Order enter/stay/exit so a script
  // sees begin-before-persist and never a stale contact after it ended.
  for (const auto& pair : m_collisionSystem->getCollisionEnters())
  {
    m_scriptSystem->dispatchCollisionEvent(objectManager, pair.a, pair.b, CollisionEvent::enter);
  }

  for (const auto& pair : m_collisionSystem->getCollisionStays())
  {
    m_scriptSystem->dispatchCollisionEvent(objectManager, pair.a, pair.b, CollisionEvent::stay);
  }

  for (const auto& pair : m_collisionSystem->getCollisionExits())
  {
    m_scriptSystem->dispatchCollisionEvent(objectManager, pair.a, pair.b, CollisionEvent::exit);
  }
}
