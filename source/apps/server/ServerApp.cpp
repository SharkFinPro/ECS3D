#include "ServerApp.h"

ServerApp::ServerApp(LaunchOptions options)
  : m_options(std::move(options))
{
  // TODO: build the authoritative simulation:
  // TODO:   - ManagedHost: boot the CoreCLR runtime (for the net transport + ScriptBridge).
  // TODO:   - ComponentRegistry: register every component factory (ECS3DData).
  // TODO:   - SceneManager: load m_options.project via the project serializer.
  // TODO:   - PhysicsSystem / CollisionSystem / ScriptSystem: the number crunching.
  // TODO:   - NetServer: start(m_options.port, m_options.editMode). editMode is the launch gate.
}

bool ServerApp::isActive() const
{
  // TODO: stay active while at least one client is connected (or always, for a dedicated server).
  return true;
}

void ServerApp::run()
{
  // TODO: the server owns the loop that used to live in ECS3D::update/fixedUpdate. Each pass:
  // TODO:   1. drain NetServer inbox (client input / editor edit commands, auth-gated by role).
  // TODO:   2. step the accumulator below, running fixedUpdate at the fixed timestep.
  // TODO:   3. serialize() the changed Transform/velocity state and broadcast StateDeltas;
  // TODO:      send a full Snapshot to any client that just joined.
  while (isActive())
  {
    // TODO: float dt = elapsed since last pass (see ECS3D::fixedUpdate accumulator).
    constexpr float dt = 0.0f;

    m_timeAccumulator += dt;

    uint8_t steps = 1;
    while (m_timeAccumulator >= m_fixedUpdateDt && steps <= 3)
    {
      ++steps;

      fixedUpdate(m_fixedUpdateDt);

      m_timeAccumulator -= m_fixedUpdateDt;
    }
  }
}

void ServerApp::fixedUpdate(const float dt)
{
  // TODO: run the systems against the current scene in order: scripts -> physics -> collisions
  // TODO:   (resolution), passing a SimContext that exposes logging / input / uuids.
  (void)dt;
}
