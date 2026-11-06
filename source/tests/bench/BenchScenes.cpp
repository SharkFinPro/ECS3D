#include "BenchScenes.h"

#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include <glm/vec3.hpp>
#include <stdexcept>
#include <string>

namespace bench {
  namespace {
    using fixtures::Scene;

    struct Lcg {
      uint32_t state;

      float next(const float low, const float high)
      {
        state = state * 1664525u + 1013904223u;
        return low + (high - low) * static_cast<float>((state >> 8) & 0xFFFFu) / 65535.0f;
      }
    };

    // A unit box is two units on a side at scale 1, so a scale of half the size gives that size.
    void addStaticBox(const Scene& scene, const std::string& name, const glm::vec3& center, const glm::vec3& size)
    {
      const auto object = fixtures::addObject(scene, name, center, size * 0.5f);
      fixtures::addBoxCollider(object);
    }

    void addDynamicBox(const Scene& scene, const std::string& name, const glm::vec3& center, const float size)
    {
      const auto object = fixtures::addObject(scene, name, center, glm::vec3(size * 0.5f));
      fixtures::addBoxCollider(object);
      fixtures::addRigidBody(object);
    }

    void addDynamicSphere(const Scene& scene, const std::string& name, const glm::vec3& center, const float radius)
    {
      const auto object = fixtures::addObject(scene, name, center);
      fixtures::addSphereCollider(object, radius);
      fixtures::addRigidBody(object);
    }

    void addDynamicCapsule(const Scene& scene, const std::string& name, const glm::vec3& center, const float radius,
                           const float height)
    {
      const auto object = fixtures::addObject(scene, name, center);
      fixtures::addCapsuleCollider(object, radius, height);
      fixtures::addRigidBody(object);
    }

    void addGround(const Scene& scene, const float halfWidth)
    {
      addStaticBox(scene, "Ground", { 0.0f, -0.5f, 0.0f }, { halfWidth * 2.0f, 1.0f, halfWidth * 2.0f });
    }

    void buildPile(const Scene& scene, const SceneOptions& options)
    {
      constexpr float spacing = 1.25f;
      const float half = static_cast<float>(options.size) * spacing * 0.5f;
      addGround(scene, half + 20.0f);

      Lcg random{ options.seed };
      for (int layer = 0; layer < options.layers; ++layer)
      {
        for (int x = 0; x < options.size; ++x)
        {
          for (int z = 0; z < options.size; ++z)
          {
            const glm::vec3 jitter{ random.next(-0.1f, 0.1f), 0.0f, random.next(-0.1f, 0.1f) };
            const glm::vec3 center{ static_cast<float>(x) * spacing - half, 1.0f + static_cast<float>(layer) * spacing,
                                    static_cast<float>(z) * spacing - half };
            addDynamicBox(scene, "Box", center + jitter, 1.0f);
          }
        }
      }
    }

    void buildScatter(const Scene& scene, const SceneOptions& options)
    {
      const int count = options.size * 100;
      const float half = 6.0f * static_cast<float>(options.size);
      addGround(scene, half + 10.0f);

      Lcg random{ options.seed };
      for (int i = 0; i < count; ++i)
      {
        const glm::vec3 center{ random.next(-half, half), random.next(1.0f, 12.0f), random.next(-half, half) };
        if (i % 3 == 0)
        {
          addDynamicSphere(scene, "Ball", center, 0.5f);
        }
        else
        {
          addDynamicBox(scene, "Box", center, 1.0f);
        }
      }
    }

    void buildTiles(const Scene& scene, const SceneOptions& options)
    {
      constexpr float tile = 2.0f;
      const float half = static_cast<float>(options.size) * tile * 0.5f;
      for (int x = 0; x < options.size; ++x)
      {
        for (int z = 0; z < options.size; ++z)
        {
          addStaticBox(scene, "Tile", { static_cast<float>(x) * tile - half + 1.0f, -0.5f,
                                        static_cast<float>(z) * tile - half + 1.0f }, { tile, 1.0f, tile });
        }
      }

      Lcg random{ options.seed };
      const int bodies = options.size * options.size / 4;
      for (int i = 0; i < bodies; ++i)
      {
        const glm::vec3 center{ random.next(-half + 1.0f, half - 1.0f), random.next(1.0f, 8.0f),
                                random.next(-half + 1.0f, half - 1.0f) };
        addDynamicBox(scene, "Box", center, 0.8f);
      }
    }

    void buildWall(const Scene& scene, const SceneOptions& options)
    {
      constexpr float spacing = 1.02f;
      const float half = static_cast<float>(options.size) * spacing * 0.5f;
      addGround(scene, half + 10.0f);

      for (int layer = 0; layer < options.layers; ++layer)
      {
        for (int z = 0; z < options.size; ++z)
        {
          addDynamicBox(scene, "Brick", { 0.0f, 0.51f + static_cast<float>(layer) * spacing,
                                          static_cast<float>(z) * spacing - half }, 1.0f);
        }
      }
    }

    void buildMixed(const Scene& scene, const SceneOptions& options)
    {
      const float half = 3.0f * static_cast<float>(options.size) + 10.0f;
      addGround(scene, half);
      addStaticBox(scene, "WallN", { 0.0f, 2.0f, half }, { half * 2.0f, 4.0f, 1.0f });
      addStaticBox(scene, "WallS", { 0.0f, 2.0f, -half }, { half * 2.0f, 4.0f, 1.0f });
      addStaticBox(scene, "WallE", { half, 2.0f, 0.0f }, { 1.0f, 4.0f, half * 2.0f });
      addStaticBox(scene, "WallW", { -half, 2.0f, 0.0f }, { 1.0f, 4.0f, half * 2.0f });

      Lcg random{ options.seed };
      const int clutter = options.size * 10;
      for (int i = 0; i < clutter; ++i)
      {
        const float size = random.next(0.5f, 4.0f);
        addStaticBox(scene, "Clutter", { random.next(-half + 3.0f, half - 3.0f), size * 0.5f,
                                         random.next(-half + 3.0f, half - 3.0f) }, glm::vec3(size));
      }

      const int bodies = options.size * 100;
      for (int i = 0; i < bodies; ++i)
      {
        const glm::vec3 center{ random.next(-half + 2.0f, half - 2.0f), random.next(5.0f, 25.0f),
                                random.next(-half + 2.0f, half - 2.0f) };
        switch (i % 4)
        {
        case 0:
          addDynamicSphere(scene, "Ball", center, random.next(0.25f, 1.0f));
          break;
        case 1:
          addDynamicCapsule(scene, "Capsule", center, 0.3f, 1.5f);
          break;
        default:
          addDynamicBox(scene, "Box", center, random.next(0.4f, 2.0f));
          break;
        }
      }
    }
  }

  fixtures::Scene buildScene(const SceneOptions& options)
  {
    auto scene = fixtures::makeScene();

    if (options.name == "pile")
    {
      buildPile(scene, options);
    }
    else if (options.name == "scatter")
    {
      buildScatter(scene, options);
    }
    else if (options.name == "tiles")
    {
      buildTiles(scene, options);
    }
    else if (options.name == "wall")
    {
      buildWall(scene, options);
    }
    else if (options.name == "mixed")
    {
      buildMixed(scene, options);
    }
    else
    {
      throw std::invalid_argument("unknown scene '" + options.name + "'");
    }

    return scene;
  }

  std::vector<std::string> sceneNames()
  {
    return { "pile", "scatter", "tiles", "wall", "mixed" };
  }
}
