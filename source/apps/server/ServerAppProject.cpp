#include "ServerApp.h"
#include <ProjectSerializer.h>
#include <ProjectPacker.h>
#include <assets/AssetRegistry.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <CollisionSystem.h>
#include <ScriptSystem.h>
#include <Log.h>
#include <nlohmann/json.hpp>
#include <string>

void ServerApp::handleLoadProject(const net::Message& message) const
{
  // An editor opened a different project: stop the current scripts, swap the project in, restart,
  // and snapshot so every view rebuilds. The blob is sent (not a path) so it works off-machine too.
  Log::info(LogCategory::server, "Received loadProject (" + std::to_string(message.bytes().size()) + " bytes).");

  // Opening/creating a project must not start the sim. Read the status before unpack(), which clears
  // the SceneManager and resets status to stopped as a side effect.
  const bool wasRunning = m_sceneManager->getSceneStatus() == SceneStatus::running;

  // Stop the current scripts before the scene is swapped out from under them.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    stopScriptsLogged(*scene->getObjectManager());
  }

  try
  {
    // Same packed shape as a snapshot: unpack clears + rebuilds the AssetRegistry/SceneManager and loads
    // the current scene. ProjectSerializer stays the JSON path for file save/load.
    m_projectPacker->unpack(message);
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, std::string("Failed to load project from editor: ") + e.what());

    // unpack parses into locals and only swaps on failure-free completion, so a throw leaves the current
    // scene untouched - the same one whose scripts were just stopped. Restart them only if it was running.
    if (wasRunning)
    {
      if (const auto scene = m_sceneManager->getCurrentScene())
      {
        startScriptsLogged(*scene->getObjectManager());
      }
    }

    return;
  }

  finishProjectLoad(wasRunning);
}

void ServerApp::finishProjectLoad(const bool wasRunning) const
{
  // New project/scene: any contact history belongs to the project we just swapped out.
  m_collisionSystem->reset();

  // Opening/creating a project starts stopped, matching the scene-switch path: only resume the sim if
  // it was actually running before the swap.
  if (wasRunning)
  {
    m_sceneManager->startScene();
  }

  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    if (wasRunning)
    {
      startScriptsLogged(*scene->getObjectManager());
    }

    Log::info(LogCategory::server, "Loaded project from editor: scene '" + scene->getName() + "' ("
      + std::to_string(scene->getObjectManager()->getAllObjects().size()) + " objects, "
      + (wasRunning ? "running" : "stopped") + ").");
  }

  broadcastSnapshot();
}

void ServerApp::startScriptsLogged(ObjectManager& objectManager) const
{
  try
  {
    m_scriptSystem->start(objectManager);
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, e.what());
  }
}

void ServerApp::stopScriptsLogged(ObjectManager& objectManager) const
{
  try
  {
    m_scriptSystem->stop(objectManager);
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, e.what());
  }
}

void ServerApp::loadScene(const std::string& sceneUUID) const
{
  const auto parsed = uuids::uuid::from_string(sceneUUID);
  if (!parsed.has_value())
  {
    return;
  }

  const auto scene = m_sceneManager->getScene(parsed.value());
  if (!scene)
  {
    return;
  }

  // A switch keeps a stopped sim stopped. Read the status before loadScene() resets it to stopped; paused
  // also lands stopped, since the new scene was never started and there is nothing live to resume.
  const bool wasRunning = m_sceneManager->getSceneStatus() == SceneStatus::running;

  // Stop the outgoing scene's scripts before switching the active scene.
  if (const auto current = m_sceneManager->getCurrentScene())
  {
    try
    {
      m_scriptSystem->stop(*current->getObjectManager());
    }
    catch (const std::exception& e)
    {
      Log::error(LogCategory::server, e.what());
    }
  }

  m_sceneManager->loadScene(scene);

  // New scene: contact history from the previous scene is meaningless here.
  m_collisionSystem->reset();

  if (wasRunning)
  {
    m_sceneManager->startScene();

    try
    {
      m_scriptSystem->start(*scene->getObjectManager());
    }
    catch (const std::exception& e)
    {
      Log::error(LogCategory::server, e.what());
    }
  }

  Log::info(LogCategory::server, "Switched to scene '" + scene->getName() + "' ("
    + std::to_string(scene->getObjectManager()->getAllObjects().size()) + " objects, "
    + (wasRunning ? "running" : "stopped") + ").");

  broadcastSnapshot();
}
