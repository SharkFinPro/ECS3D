#include "BenchScenes.h"

#include "CollisionSystem.h"
#include "Lane.h"
#include "PhysicsSystem.h"
#include "ProfileScope.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/Collider.h"
#include <glm/vec3.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Steps a seeded scene through PhysicsSystem and CollisionSystem the way ServerApp does, and reports the
// mean time per tick of each collision phase. Not a test: build it with
//   cmake --build <build-dir> --target ECS3DCollisionBench
namespace {
  constexpr float dt = 1.0f / 50.0f;

  struct Options {
    bench::SceneOptions scene;
    int ticks = 300;
    int warmup = 0;
  };

  Options parseOptions(const int argc, char** argv)
  {
    Options options;
    for (int i = 1; i < argc; ++i)
    {
      const std::string flag = argv[i];
      if (i + 1 >= argc)
      {
        throw std::invalid_argument("flag '" + flag + "' needs a value");
      }

      const std::string value = argv[++i];
      if (flag == "--scene")
      {
        options.scene.name = value;
      }
      else if (flag == "--size")
      {
        options.scene.size = std::stoi(value);
      }
      else if (flag == "--layers")
      {
        options.scene.layers = std::stoi(value);
      }
      else if (flag == "--seed")
      {
        options.scene.seed = static_cast<uint32_t>(std::stoul(value));
      }
      else if (flag == "--ticks")
      {
        options.ticks = std::stoi(value);
      }
      else if (flag == "--warmup")
      {
        options.warmup = std::stoi(value);
      }
      else
      {
        throw std::invalid_argument("unknown flag '" + flag + "'");
      }
    }

    if (options.ticks < 1 || options.warmup < 0 || options.scene.size < 1 || options.scene.layers < 1)
    {
      throw std::invalid_argument("ticks, size and layers must be at least 1");
    }

    return options;
  }

  struct Totals {
    double physics = 0.0;
    double collisions = 0.0;
    std::map<std::string, double> phases;
    size_t contacts = 0;
  };

  // FNV-1a over every object's final position and rotation bits: two builds that print the same value
  // stepped the scene identically.
  uint64_t transformChecksum(const ObjectManager& objects)
  {
    uint64_t hash = 14695981039346656037ull;
    const auto mix = [&hash](const glm::vec3& value) {
      unsigned char bytes[sizeof(glm::vec3)];
      std::memcpy(bytes, &value, sizeof(bytes));
      for (const unsigned char byte : bytes)
      {
        hash = (hash ^ byte) * 1099511628211ull;
      }
    };

    for (const auto& object : objects.getAllObjects())
    {
      if (const auto transform = object->getComponent<Transform>(ComponentType::transform))
      {
        mix(transform->getPosition());
        mix(transform->getRotation());
      }
    }

    return hash;
  }

  // Nested scopes are reported as "parent/child", so they sort under the phase they belong to.
  void addPhases(const profile::Lane& lane, const profile::NodeId parent, const std::string& prefix, Totals& totals)
  {
    const auto& sample = lane.history().newest();
    for (const auto node : lane.tree().children(parent))
    {
      const auto name = prefix + std::string(profile::nameOf(lane.tree().name(node)));
      totals.phases[name] += sample.nodeMicros[node];
      addPhases(lane, node, name + "/", totals);
    }
  }
}

int main(const int argc, char** argv)
{
  try
  {
    const auto options = parseOptions(argc, argv);
    const auto scene = bench::buildScene(options.scene);
    auto& objects = *scene.objectManager;

    size_t colliders = 0;
    size_t bodies = 0;
    for (const auto& object : objects.getAllObjects())
    {
      colliders += object->getComponent<Collider>(ComponentType::collider) != nullptr;
      bodies += object->getComponent<RigidBody>(ComponentType::rigidBody) != nullptr;
    }

    CollisionSystem collisions;
    profile::setEnabled(true);
    profile::Lane lane("bench", 4);
    lane.bindToThisThread();

    Totals totals;
    using Clock = std::chrono::steady_clock;
    for (int tick = 0; tick < options.warmup + options.ticks; ++tick)
    {
      const auto start = Clock::now();
      PhysicsSystem::fixedUpdate(objects, dt);
      const auto physicsEnd = Clock::now();

      lane.beginSample();
      collisions.fixedUpdate(objects, dt);
      lane.endSample(true);
      const auto end = Clock::now();

      if (tick < options.warmup)
      {
        continue;
      }

      totals.physics += std::chrono::duration<double, std::micro>(physicsEnd - start).count();
      totals.collisions += std::chrono::duration<double, std::micro>(end - physicsEnd).count();
      totals.contacts += collisions.getCollisionEnters().size() + collisions.getCollisionStays().size();
      addPhases(lane, 0, "", totals);
    }

    profile::Lane::unbindThisThread();

    const double ticks = options.ticks;
    std::cout << std::fixed << std::setprecision(1)
              << "scene=" << options.scene.name << " size=" << options.scene.size
              << " layers=" << options.scene.layers << " seed=" << options.scene.seed
              << " colliders=" << colliders << " bodies=" << bodies << " ticks=" << options.ticks
              << " warmup=" << options.warmup << " checksum=" << std::hex << transformChecksum(objects) << std::dec
              << "\n"
              << "  contacts/tick " << static_cast<double>(totals.contacts) / ticks << "\n"
              << "  physics us/tick " << totals.physics / ticks << "\n"
              << "  collisions us/tick " << totals.collisions / ticks << "\n";
    for (const auto& [name, micros] : totals.phases)
    {
      std::cout << "    " << name << " us/tick " << micros / ticks << "\n";
    }

    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "ECS3DCollisionBench: " << error.what() << "\n";
    return 1;
  }
}
