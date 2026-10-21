#include "DefaultWelcome.h"
#include "DefaultProjectParts.h"
#include "DefaultShapes.h"
#include "objects/components/RotationConvention.h"
#include <nlohmann/json.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;
using namespace defaultproject;

namespace {
  // The Shape Shelf is a slab; its top is the plane the library shapes rest on.
  const glm::vec3 shelfCenter(-17.0f, 0.5f, 22.0f);
  const glm::vec3 shelfScale(1.5f, 0.5f, 4.0f);
  constexpr float shapeScale = 0.5f;

  json named(json object, const std::string& name)
  {
    object["name"] = name;

    return object;
  }

  json retextured(json object, const std::string& texture)
  {
    for (auto& component : object.at("components"))
    {
      if (component.at("type") == "ModelRenderer")
      {
        component["textureUUID"] = texture;
        component["specularMapUUID"] = whiteTexture;
      }
    }

    return object;
  }

  json hidden(json object)
  {
    for (auto& component : object.at("components"))
    {
      if (component.at("type") == "ModelRenderer")
      {
        component["shouldRender"] = false;
      }
    }

    return object;
  }

  json withoutRigidBody(json object)
  {
    json kept = json::array();
    for (const auto& component : object.at("components"))
    {
      if (component.at("type") != "RigidBody")
      {
        kept.push_back(component);
      }
    }

    object["components"] = std::move(kept);

    return object;
  }

  json withFriction(json object, const float friction)
  {
    for (auto& component : object.at("components"))
    {
      if (component.at("type") == "RigidBody")
      {
        component["friction"] = friction;
      }
    }

    return object;
  }

  json field(const std::string& name, const std::string& type, json value)
  {
    json entry;
    entry["name"] = name;
    entry["type"] = type;
    entry["value"] = std::move(value);

    return entry;
  }

  json floatField(const std::string& name, const float value)
  {
    return field(name, "float", value);
  }

  json intField(const std::string& name, const int value)
  {
    return field(name, "int", value);
  }

  json stringField(const std::string& name, const std::string& value)
  {
    return field(name, "string", value);
  }

  json boolField(const std::string& name, const bool value)
  {
    return field(name, "bool", value);
  }

  json vectorField(const std::string& name, const glm::vec3& value)
  {
    return field(name, "vector3", vec(value));
  }

  json script(const std::string& className, json fields)
  {
    json entry;
    entry["type"] = "Script";
    entry["className"] = className;
    entry["fields"] = std::move(fields);

    return entry;
  }

  json withScripts(json object, std::initializer_list<json> scripts)
  {
    for (const auto& entry : scripts)
    {
      object["scripts"].push_back(entry);
    }

    return object;
  }

  json emptyObject(const std::string& name, const glm::vec3& position)
  {
    return makeObject(name, json::array({ transform(position) }));
  }

  json child(json parent, json object)
  {
    parent["children"].push_back(std::move(object));

    return parent;
  }

  json sceneLight(const std::string& name, const glm::vec3& position, const glm::vec3& color,
                  const float ambient, const float diffuse, const float specular)
  {
    return named(light(position, color, ambient, diffuse, specular), name);
  }

  void append(json& objects, const json& more)
  {
    for (const auto& object : more)
    {
      objects.push_back(object);
    }
  }

  // Four by four tiles of 20 x 20 under one invisible collider. The tiles inherit the collider's scale, so
  // their local scale and offsets divide it back out.
  json buildGround()
  {
    constexpr float halfSize = 40.0f;
    constexpr float tileSize = 20.0f;
    constexpr float localHalfTile = tileSize / 2.0f / halfSize;

    auto ground = hidden(named(rigidBlock({ 0.0f, -1.0f, 0.0f }, { halfSize, 1.0f, halfSize }), "Ground"));

    for (int row = 0; row < 4; ++row)
    {
      for (int column = 0; column < 4; ++column)
      {
        const float x = (static_cast<float>(column) - 1.5f) * tileSize / halfSize;
        const float z = (static_cast<float>(row) - 1.5f) * tileSize / halfSize;

        ground = child(std::move(ground), makeObject("Ground Tile", json::array({
          transform({ x, 1.0f, z }, { localHalfTile, 1.0f, localHalfTile }),
          modelRenderer(planeTileModel, gridCheckerTexture, whiteTexture)
        })));
      }
    }

    return json::array({ ground });
  }

  json buildHero()
  {
    const auto satellite = makeObject("Satellite", json::array({
      transform({ 0.0f, 0.0f, 1.5f }, { 0.06f, 0.06f, 0.12f }),
      modelRenderer(cubeModel, whiteTexture, whiteTexture)
    }));

    const auto earth = child(makeObject("Earth", json::array({
      transform({ 0.0f, 8.0f, -16.0f }, glm::vec3(4.0f)),
      modelRenderer(sphereModel, earthTexture, earthSpecularTexture)
    }), json::array({
      script("Spinner", json::array({
        vectorField("m_axis", { 0.0f, 1.0f, 0.0f }),
        floatField("m_degreesPerSecond", 10.0f)
      }))
    })), satellite);

    const auto moon = makeObject("Moon", json::array({
      transform({ 9.0f, 8.0f, -16.0f }, glm::vec3(1.2f)),
      modelRenderer(sphereModel, whiteTexture, whiteTexture)
    }), json::array({
      script("Orbiter", json::array({
        stringField("m_centerObject", "Earth"),
        floatField("m_radius", 9.0f),
        floatField("m_periodSeconds", 20.0f)
      }))
    }));

    return json::array({ earth, moon });
  }

  json buildLights()
  {
    // DayNightCycle owns the Sun's position from the first tick; this is where it stands at its Start Time.
    constexpr float startTime = 0.3f;
    constexpr float arcRadius = 40.0f;
    const float angle = 2.0f * std::numbers::pi_v<float> * startTime;
    const glm::vec3 sunPosition(arcRadius * std::cos(angle), arcRadius * std::sin(angle), 0.0f);

    json objects = json::array({
      sceneLight("Sun", sunPosition, { 1.0f, 0.97f, 0.9f }, 0.25f, 0.8f, 0.5f)
    });

    struct Accent
    {
      const char* name;
      glm::vec3 position;
      glm::vec3 color;
      float phase;
    };

    const Accent accents[] = {
      { "Light Red", { -10.0f, 7.0f, -22.0f }, { 1.0f, 0.15f, 0.15f }, 0.0f },
      { "Light Green", { 10.0f, 7.0f, -22.0f }, { 0.15f, 1.0f, 0.2f }, 120.0f },
      { "Light Blue", { 0.0f, 7.0f, -29.0f }, { 0.2f, 0.35f, 1.0f }, 240.0f }
    };

    for (const auto& accent : accents)
    {
      objects.push_back(withScripts(sceneLight(accent.name, accent.position, accent.color, 0.0f, 0.75f, 0.75f), {
        script("Bobber", json::array({
          floatField("m_amplitude", 1.5f),
          floatField("m_frequencyHz", 0.2f),
          floatField("m_phaseDegrees", accent.phase)
        }))
      }));
    }

    return objects;
  }

  // 24 one-unit blocks, 2 x 2 x 6, each face exactly against its neighbors and the ground.
  json buildBlockTower()
  {
    json objects = json::array();

    for (int layer = 0; layer < 6; ++layer)
    {
      for (int x = 0; x < 2; ++x)
      {
        for (int z = 0; z < 2; ++z)
        {
          const glm::vec3 position(-31.5f + static_cast<float>(x),
                                   0.5f + static_cast<float>(layer),
                                   -0.5f + static_cast<float>(z));

          objects.push_back(retextured(named(block(position, glm::vec3(0.5f)), "Tower Block"),
                                       checkerOrangeTexture));
        }
      }
    }

    return objects;
  }

  json spawner(const std::string& name, const glm::vec3& position, const std::string& prefab,
               const float interval, const int maxAlive, const float radius)
  {
    return withScripts(emptyObject(name, position), {
      script("Spawner", json::array({
        stringField("m_prefab", prefab),
        floatField("m_interval", interval),
        intField("m_maxAlive", maxAlive),
        floatField("m_offsetRadius", radius)
      }))
    });
  }

  json buildRampAndPad()
  {
    const auto ramp = named(instanceOf(rampBody(), { -25.0f, 3.0f, 10.0f }, { 7.0f, 3.0f, 2.0f },
                                       { 0.0f, 180.0f, 0.0f }), "Ramp");

    const auto pad = withScripts(retextured(named(rigidBlock({ -25.0f, 0.25f, 22.0f }, { 2.5f, 0.25f, 2.5f }),
                                                  "Bounce Pad"), paletteGreenTexture), {
      script("BouncePad", json::array({ floatField("m_strength", 12.0f) })),
      script("ColorOnHit", json::array({ stringField("m_hitTexture", paletteRedTexture) }))
    });

    return json::array({
      ramp,
      spawner("Ball Spawner", { -31.0f, 7.5f, 9.0f }, spherePrefab, 3.0f, 4, 0.5f),
      spawner("Capsule Spawner", { -31.0f, 7.5f, 11.0f }, capsulePrefab, 3.0f, 4, 0.5f),
      pad,
      spawner("Dice Spawner", { -25.0f, 14.0f, 22.0f }, d20Prefab, 1.5f, 8, 1.2f)
    });
  }

  struct RestPose
  {
    glm::vec3 rotation;
    float lowest;
  };

  struct HullFacet
  {
    glm::vec3 normal;
    float area;
  };

  // The hull's flat faces, with coplanar triangles merged and their areas summed.
  std::vector<HullFacet> facetsOf(const ConvexHull& hull)
  {
    std::vector<HullFacet> facets;

    for (size_t i = 0; i + 2 < hull.indices.size(); i += 3)
    {
      const auto& a = hull.vertices[hull.indices[i]];
      const auto cross = glm::cross(hull.vertices[hull.indices[i + 1]] - a, hull.vertices[hull.indices[i + 2]] - a);
      const float length = glm::length(cross);
      const auto normal = cross / length;
      const auto match = std::find_if(facets.begin(), facets.end(), [&normal](const HullFacet& facet) {
        return glm::dot(facet.normal, normal) > 0.9999f;
      });

      if (match != facets.end())
      {
        match->area += length / 2.0f;
      }
      else
      {
        facets.push_back({ normal, length / 2.0f });
      }
    }

    return facets;
  }

  // Turns the hull's largest face to point straight down; lowest is then its lowest vertex at unit scale.
  RestPose restPose(const ConvexHull& hull)
  {
    const auto facets = facetsOf(hull);
    const auto largest = std::max_element(facets.begin(), facets.end(), [](const HullFacet& a, const HullFacet& b) {
      return a.area < b.area;
    });

    const glm::vec3 down(0.0f, -1.0f, 0.0f);
    const auto axis = glm::cross(largest->normal, down);
    const float angle = std::acos(std::clamp(glm::dot(largest->normal, down), -1.0f, 1.0f));
    const auto turn = glm::length(axis) < 1e-6f ? glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f))
                                                : glm::angleAxis(angle, glm::normalize(axis));

    const auto rotation = quatToEulerDegrees(turn);
    const auto orientation = eulerDegreesToQuat(rotation);
    float lowest = (orientation * hull.vertices.front()).y;
    for (const auto& vertex : hull.vertices)
    {
      lowest = std::min(lowest, (orientation * vertex).y);
    }

    return { rotation, lowest };
  }

  json buildShapeShelf()
  {
    const float top = shelfCenter.y + shelfScale.y;

    json objects = json::array({
      retextured(named(rigidBlock(shelfCenter, shelfScale), "Shape Shelf"), checkerBlueTexture)
    });

    int index = 0;
    for (const auto& shape : libraryShapes())
    {
      const auto hull = shape.hull();
      const auto pose = restPose(hull);
      const glm::vec3 position(shelfCenter.x + (index % 2 == 0 ? -0.75f : 0.75f),
                               top - pose.lowest * shapeScale,
                               shelfCenter.z + (static_cast<float>(index / 2) - 2.0f) * 1.6f);
      const float mass = hullVolume(hull) * shapeScale * shapeScale * shapeScale;

      objects.push_back(withMass(instanceOf(libraryBody(shape.name), position, glm::vec3(shapeScale), pose.rotation),
                                 mass));
      ++index;
    }

    return objects;
  }

  json buildPhysicsCorner()
  {
    json objects = buildBlockTower();
    append(objects, buildRampAndPad());
    append(objects, buildShapeShelf());

    return objects;
  }

  json spinnerRing(const std::string& name, const float z, const glm::vec3& axis, const float degrees)
  {
    return withScripts(makeObject(name, json::array({
      transform({ 32.0f, 3.0f, z }),
      modelRenderer(torusModel, palettePurpleTexture, whiteTexture)
    })), {
      script("Spinner", json::array({ vectorField("m_axis", axis), floatField("m_degreesPerSecond", degrees) }))
    });
  }

  json mover(const std::string& name, const glm::vec3& position, const glm::vec3& scale, const glm::vec3& offset,
             const float speed)
  {
    return withScripts(retextured(named(rigidBlock(position, scale), name), palettePurpleTexture), {
      script("PingPongMover", json::array({
        vectorField("m_offset", offset),
        floatField("m_speed", speed),
        floatField("m_pauseSeconds", 1.5f),
        boolField("m_carryRiders", true)
      }))
    });
  }

  json buildMotionGarden()
  {
    return json::array({
      spinnerRing("Spinner Ring X", -11.0f, { 1.0f, 0.0f, 0.0f }, 30.0f),
      spinnerRing("Spinner Ring Y", -8.0f, { 0.0f, 1.0f, 0.0f }, 60.0f),
      spinnerRing("Spinner Ring Z", -5.0f, { 0.0f, 0.0f, 1.0f }, 90.0f),
      mover("Lift", { 20.0f, 0.25f, 4.0f }, { 2.0f, 0.25f, 2.0f }, { 0.0f, 6.0f, 0.0f }, 1.5f),
      mover("Ferry", { 14.0f, 0.25f, 12.0f }, { 2.0f, 0.25f, 2.0f }, { 12.0f, 0.0f, 0.0f }, 2.0f),
      retextured(named(rigidBlock({ 29.0f, 3.0f, 4.0f }, { 4.0f, 3.0f, 4.0f }), "Ledge"), checkerBlueTexture)
    });
  }

  json buildWatcher()
  {
    const auto nose = makeObject("Watcher Nose", json::array({
      transform({ 0.0f, 0.0f, -1.2f }, { 0.3f, 0.3f, 0.5f }),
      modelRenderer(cubeModel, paletteRedTexture, whiteTexture)
    }));

    const auto body = withScripts(withoutRigidBody(named(block({ 30.0f, 7.0f, 3.0f }, glm::vec3(0.7f)), "Watcher")), {
      script("LookAtTarget", json::array({
        intField("m_targetPlayerSlot", 0),
        floatField("m_turnSpeed", 120.0f),
        boolField("m_yawOnly", false)
      })),
      script("LaserPointer", json::array({ stringField("m_marker", "LaserMarker") }))
    });

    const auto marker = makeObject("LaserMarker", json::array({
      transform({ 30.0f, 6.15f, 5.0f }, glm::vec3(0.15f)),
      modelRenderer(sphereModel, paletteRedTexture, whiteTexture)
    }));

    return json::array({ child(body, nose), marker });
  }

  json buildPlayers()
  {
    const auto buddy = withScripts(withFriction(retextured(named(sphere({ 19.0f, 0.6f, 18.0f }, glm::vec3(0.6f)),
                                                                 "Buddy"), checkerOrangeTexture), 0.0f), {
      script("Follower", json::array({
        intField("m_targetPlayerSlot", 0),
        floatField("m_speed", 5.0f),
        floatField("m_stopDistance", 2.5f)
      }))
    });

    return json::array({
      buddy,
      player({ 0.0f, 1.0f, 18.0f }, 0, glm::normalize(glm::vec3(0.0f, 7.0f, -34.0f))),
      emptyObject("SpawnPoint1", { 0.0f, 1.0f, 18.0f }),
      emptyObject("SpawnPoint2", { -6.0f, 1.0f, 18.0f }),
      emptyObject("SpawnPoint3", { 6.0f, 1.0f, 18.0f }),
      emptyObject("SpawnPoint4", { 0.0f, 1.0f, 24.0f })
    });
  }
}

json buildWelcomeObjects()
{
  json objects = json::array();

  append(objects, buildGround());
  append(objects, buildHero());
  append(objects, buildLights());
  append(objects, buildPhysicsCorner());
  append(objects, buildMotionGarden());
  append(objects, buildWatcher());
  append(objects, buildPlayers());

  return objects;
}

json buildWelcomeSceneScripts()
{
  return json::array({
    script("DayNightCycle", json::array({
      stringField("m_light", "Sun"),
      floatField("m_periodSeconds", 120.0f)
    })),
    script("PlayerSpawnManager", json::array())
  });
}
