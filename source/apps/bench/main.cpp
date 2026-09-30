#include "DefaultProject.h"
#include <CollisionSystem.h>
#include <ComponentRegistration.h>
#include <ComponentRegistry.h>
#include <ConsoleSink.h>
#include <Log.h>
#include <PhysicsSystem.h>
#include <ProjectSerializer.h>
#include <assets/AssetRegistry.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/RigidBody.h>
#include <scenes/SceneAsset.h>
#include <scenes/SceneManager.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
  // The server's fixed tick.
  constexpr float dt = 1.0f / 50.0f;

  struct BenchOptions {
    int ticks = 1500;
    Scene3Options scene3;
    int threads = 1;
    BroadPhaseMode broadPhase = BroadPhaseMode::tree;
    bool refresh = true;
    bool sleep = true;
    bool parallelResponse = false;
    bool diagnostics = false;
    float sleepLinear = sleeping::linearSleepSpeed;
    float sleepAngular = sleeping::angularSleepSpeed;
    uint32_t sleepTicks = sleeping::ticksToSleep;
    SleepSupport sleepSupport = SleepSupport::falling;
    SleepMode sleepMode = SleepMode::island;
  };

  bool parseBool(const std::string& value)
  {
    if (value != "0" && value != "1")
    {
      throw std::invalid_argument("expected 0 or 1, got '" + value + "'");
    }

    return value == "1";
  }

  BenchOptions parseOptions(const int argc, char** argv)
  {
    BenchOptions options;
    options.scene3.seed = 1;
    options.scene3.overlapFree = true;
    options.threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency() / 2));

    for (int i = 1; i < argc; ++i)
    {
      const std::string flag = argv[i];

      if (i + 1 >= argc)
      {
        throw std::invalid_argument("flag '" + flag + "' needs a value");
      }

      const std::string value = argv[++i];

      if (flag == "--ticks")
      {
        options.ticks = std::stoi(value);
      }
      else if (flag == "--seed")
      {
        options.scene3.seed = static_cast<uint32_t>(std::stoull(value));
      }
      else if (flag == "--grid")
      {
        options.scene3.gridSize = std::stoi(value);
      }
      else if (flag == "--layers")
      {
        options.scene3.layerCount = std::stoi(value);
      }
      else if (flag == "--threads")
      {
        options.threads = std::stoi(value);
      }
      else if (flag == "--broadphase")
      {
        if (value != "sweep" && value != "tree")
        {
          throw std::invalid_argument("--broadphase takes sweep or tree, got '" + value + "'");
        }

        options.broadPhase = value == "sweep" ? BroadPhaseMode::sweep : BroadPhaseMode::tree;
      }
      else if (flag == "--refresh")
      {
        options.refresh = parseBool(value);
      }
      else if (flag == "--sleep")
      {
        options.sleep = parseBool(value);
      }
      else if (flag == "--parallel-response")
      {
        options.parallelResponse = parseBool(value);
      }
      else if (flag == "--diagnostics")
      {
        options.diagnostics = parseBool(value);
      }
      else if (flag == "--sleep-linear")
      {
        options.sleepLinear = std::stof(value);
      }
      else if (flag == "--sleep-angular")
      {
        options.sleepAngular = std::stof(value);
      }
      else if (flag == "--sleep-ticks")
      {
        options.sleepTicks = static_cast<uint32_t>(std::stoul(value));
      }
      else if (flag == "--sleep-support")
      {
        if (value != "falling" && value != "contact")
        {
          throw std::invalid_argument("--sleep-support takes falling or contact, got '" + value + "'");
        }

        options.sleepSupport = value == "contact" ? SleepSupport::contact : SleepSupport::falling;
      }
      else if (flag == "--sleep-mode")
      {
        if (value != "island" && value != "grounded")
        {
          throw std::invalid_argument("--sleep-mode takes island or grounded, got '" + value + "'");
        }

        options.sleepMode = value == "grounded" ? SleepMode::grounded : SleepMode::island;
      }
      else
      {
        throw std::invalid_argument("unknown flag '" + flag + "'");
      }
    }

    if (options.ticks < 1 || options.threads < 1 || options.scene3.gridSize < 1 || options.scene3.layerCount < 1 ||
        options.sleepTicks < 1)
    {
      throw std::invalid_argument("ticks, threads, grid, layers and sleep ticks must be at least 1");
    }

    return options;
  }

  std::string describe(const BenchOptions& options)
  {
    std::ostringstream flags;
    flags << "ticks=" << options.ticks << " seed=" << *options.scene3.seed << " grid=" << options.scene3.gridSize
          << " layers=" << options.scene3.layerCount << " threads=" << options.threads
          << " broadphase=" << (options.broadPhase == BroadPhaseMode::tree ? "tree" : "sweep")
          << " refresh=" << options.refresh << " sleep=" << options.sleep
          << " parallel_response=" << options.parallelResponse << " diagnostics=" << options.diagnostics
          << " sleep_mode=" << (options.sleepMode == SleepMode::grounded ? "grounded" : "island")
          << " sleep_support=" << (options.sleepSupport == SleepSupport::contact ? "contact" : "falling")
          << " sleep_linear=" << options.sleepLinear << " sleep_angular=" << options.sleepAngular
          << " sleep_ticks=" << options.sleepTicks;

    return flags.str();
  }

  std::shared_ptr<SceneAsset> findScene(const SceneManager& sceneManager, const std::string& name)
  {
    for (const auto& entry : sceneManager.getScenes())
    {
      if (entry.second->getName() == name)
      {
        return entry.second;
      }
    }

    return nullptr;
  }

  double percentile(const std::vector<double>& sorted, const double fraction)
  {
    const auto index = static_cast<size_t>(fraction * static_cast<double>(sorted.size() - 1));

    return sorted[index];
  }

  int run(const BenchOptions& options)
  {
    const auto componentRegistry = std::make_shared<ComponentRegistry>();
    registerDataComponents(*componentRegistry);
    AssetRegistry assetRegistry;
    SceneManager sceneManager;
    const ProjectSerializer serializer(&assetRegistry, &sceneManager, componentRegistry);

    serializer.deserialize(buildDefaultProject(options.scene3));

    const auto scene = findScene(sceneManager, "Scene 3");
    if (!scene)
    {
      throw std::runtime_error("the default project has no Scene 3");
    }

    sceneManager.loadScene(scene);
    sceneManager.startScene();

    CollisionSystem collisionSystem;
    collisionSystem.setBroadPhaseMode(options.broadPhase);
    collisionSystem.setSleepingEnabled(options.sleep);
    collisionSystem.setSleepThresholds(options.sleepLinear, options.sleepAngular, options.sleepTicks);
    collisionSystem.setSleepSupport(options.sleepSupport);
    collisionSystem.setSleepMode(options.sleepMode);
    collisionSystem.setContactRefreshEnabled(options.refresh);
    collisionSystem.setThreadCount(options.threads);
    collisionSystem.setParallelResponseEnabled(options.parallelResponse);
    collisionSystem.setDetailedTimingEnabled(options.diagnostics);
    PhysicsSystem::setThreadCount(options.threads);

    const auto& objectManager = *scene->getObjectManager();

    Log::info(LogCategory::server, "Bench: " + describe(options) + " objects="
      + std::to_string(objectManager.getAllObjects().size()));

    std::vector<double> tickMicros;
    tickMicros.reserve(static_cast<size_t>(options.ticks));

    const auto wallStart = std::chrono::steady_clock::now();

    for (int tick = 0; tick < options.ticks; ++tick)
    {
      const auto tickStart = std::chrono::steady_clock::now();

      PhysicsSystem::fixedUpdate(objectManager, dt);
      collisionSystem.fixedUpdate(objectManager, dt);

      tickMicros.push_back(std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - tickStart).count());
    }

    const double wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();

    size_t bodies = 0;
    size_t asleep = 0;
    for (const auto& object : objectManager.getAllObjects())
    {
      if (const auto body = object->getComponent<RigidBody>(ComponentType::rigidBody))
      {
        ++bodies;
        asleep += body->isAsleep() ? 1 : 0;
      }
    }

    double total = 0.0;
    for (const auto micros : tickMicros)
    {
      total += micros;
    }

    std::ranges::sort(tickMicros);

    std::ostringstream summary;
    summary << std::fixed << std::setprecision(1)
            << "SUMMARY " << describe(options) << " bodies=" << bodies << " wall_s=" << wallSeconds
            << " tick_us_mean=" << total / static_cast<double>(tickMicros.size())
            << " median=" << percentile(tickMicros, 0.5)
            << " p95=" << percentile(tickMicros, 0.95)
            << " max=" << tickMicros.back()
            << " final_asleep=" << asleep;

    std::cout << summary.str() << std::endl;

    return EXIT_SUCCESS;
  }
}

int main(const int argc, char** argv)
{
  Log::addSink(std::make_shared<ConsoleSink>());

  try
  {
    return run(parseOptions(argc, argv));
  }
  catch (const std::exception& e)
  {
    std::cerr << "ECS3DPhysicsBench: " << e.what() << std::endl;
    return 2;
  }
}
