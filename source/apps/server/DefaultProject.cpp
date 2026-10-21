#include "DefaultProject.h"
#include "DefaultProjectParts.h"
#include "DefaultShapes.h"
#include "DefaultWelcome.h"
#include <nlohmann/json.hpp>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <map>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

using nlohmann::json;
using namespace defaultproject;

namespace defaultproject {

// Masses are volumes at a density of one, so bodies of one material push each other by their size. A Block's
// half-extents are its scale, so the unit Block is 2 x 2 x 2; a unit Sphere or Player has a radius of one.
constexpr float blockMass = 8.0f;
constexpr float sphereMass = 4.18879f;
// A unit sphere (the two hemispheres) plus a radius-one cylinder two units tall.
constexpr float capsuleMass = 4.0f / 3.0f * std::numbers::pi_v<float> + 2.0f * std::numbers::pi_v<float>;

constexpr float propFriction = 0.5f;
// PlayerScript steers the player's velocity itself; friction with the ground would only fight it.
constexpr float playerFriction = 0.0f;

std::string newUUID()
{
  static std::mt19937 gen{ std::random_device{}() };
  static uuids::uuid_random_generator generator{ gen };

  return uuids::to_string(generator());
}

json vec(const glm::vec3& v)
{
  return json::array({ v.x, v.y, v.z });
}

// --- Component blobs (matching ProjectSerializer's serialized format) -------------------------------

json transform(const glm::vec3& position,
               const glm::vec3& scale,
               const glm::vec3& rotation)
{
  return {
    { "type", "Transform" },
    { "position", vec(position) },
    { "rotation", vec(rotation) },
    { "scale", vec(scale) }
  };
}

json modelRenderer(const std::string& model, const std::string& texture, const std::string& specular)
{
  return {
    { "type", "ModelRenderer" },
    { "shouldRender", true },
    { "modelUUID", model },
    { "textureUUID", texture },
    { "specularMapUUID", specular }
  };
}

json rigidBody(const float mass, const float friction)
{
  return {
    { "type", "RigidBody" },
    { "velocity", vec(glm::vec3(0)) },
    { "angularVelocity", vec(glm::vec3(0)) },
    { "friction", friction },
    { "doGravity", true },
    { "gravity", -9.81 },
    { "mass", mass }
  };
}

json boxCollider()
{
  return {
    { "type", "Collider" },
    { "subType", "Box" },
    { "position", vec(glm::vec3(0)) },
    { "rotation", vec(glm::vec3(0)) },
    { "scale", vec(glm::vec3(1)) }
  };
}

json sphereCollider()
{
  return {
    { "type", "Collider" },
    { "subType", "Sphere" },
    { "radius", 1.0 },
    { "position", vec(glm::vec3(0)) }
  };
}

json capsuleCollider()
{
  return {
    { "type", "Collider" },
    { "subType", "Capsule" },
    { "position", vec(glm::vec3(0)) },
    { "rotation", vec(glm::vec3(0)) },
    { "radius", 1.0 },
    { "height", 4.0 },
    { "renderCollider", false },
    { "isTrigger", false },
    { "layer", 0u },
    { "mask", 0xFFFFFFFFu }
  };
}

json playerController(const int slot = 0)
{
  return {
    { "type", "PlayerController" },
    { "playerSlot", slot }
  };
}

json camera(const glm::vec3& direction = glm::vec3(0, 0, -1))
{
  return {
    { "type", "Camera" },
    { "direction", vec(direction) },
    { "fov", 45.0 },
    { "nearPlane", 0.1 },
    { "farPlane", 1000.0 },
    { "active", true }
  };
}

json makeObject(const std::string& name, json components, json scripts)
{
  return {
    { "name", name },
    { "children", json::array() },
    { "components", std::move(components) },
    { "scripts", std::move(scripts) },
    { "uuid", newUUID() }
  };
}

// --- Prefab bodies -----------------------------------------------------------------------------------
//
// Each is one serialized object at the identity transform, defined exactly once. They are registered as
// real Prefab assets (see buildDefaultProject) AND are what the scenes below instantiate, so the default
// project both ships a browsable/spawnable prefab set and eats its own dog food.

const json& blockBody()
{
  static const json body = makeObject("Block", json::array({
    transform(glm::vec3(0)),
    modelRenderer(cubeModel, whiteTexture, whiteTexture),
    rigidBody(blockMass, propFriction),
    boxCollider()
  }));

  return body;
}

const json& rigidBlockBody()
{
  static const json body = makeObject("Rigid Block", json::array({
    transform(glm::vec3(0)),
    modelRenderer(cubeModel, whiteTexture, whiteTexture),
    boxCollider()
  }));

  return body;
}

const json& sphereBody()
{
  static const json body = makeObject("Sphere", json::array({
    transform(glm::vec3(0)),
    modelRenderer(sphereModel, earthTexture, earthSpecularTexture),
    rigidBody(sphereMass, propFriction),
    sphereCollider()
  }));

  return body;
}

const json& capsuleBody()
{
  static const json body = makeObject("Capsule", json::array({
    transform(glm::vec3(0)),
    modelRenderer(capsuleModel, checkerOrangeTexture, whiteTexture),
    rigidBody(capsuleMass, propFriction),
    capsuleCollider()
  }));

  return body;
}

const json& cylinderBody()
{
  static const ConvexHull hull = cylinderHull();
  static const json body = makeObject("Cylinder", json::array({
    transform(glm::vec3(0)),
    modelRenderer(cylinderModel, checkerOrangeTexture, whiteTexture),
    rigidBody(hullVolume(hull), propFriction),
    convexMeshCollider(hull, cylinderModel)
  }));

  return body;
}

// A library shape: its own model, a convex mesh collider holding the model's hull, and a mass of the hull's
// volume at density one. Built once per name, so every call hands back the same body.
const json& shapeBody(const std::string& name, const std::string& model, ConvexHull (*buildHull)())
{
  static std::map<std::string, json> bodies;

  auto found = bodies.find(name);
  if (found == bodies.end())
  {
    const ConvexHull hull = buildHull();
    found = bodies.emplace(name, makeObject(name, json::array({
      transform(glm::vec3(0)),
      modelRenderer(model, checkerOrangeTexture, whiteTexture),
      rigidBody(hullVolume(hull), propFriction),
      convexMeshCollider(hull, model)
    }))).first;
  }

  return found->second;
}

const std::vector<LibraryShape>& libraryShapes()
{
  static const std::vector<LibraryShape> shapes = {
    { "D4", d4Hull }, { "D6", d6Hull }, { "D8", d8Hull }, { "D12", d12Hull }, { "D20", d20Hull },
    { "Hex Prism", hexPrismHull }, { "Drum", drumHull }, { "Gem", gemHull }, { "Boulder", boulderHull }
  };

  return shapes;
}

const json& libraryBody(const std::string& name)
{
  static const std::map<std::string, const std::string*> models = {
    { "D4", &d4Model }, { "D6", &cubeModel }, { "D8", &d8Model }, { "D12", &d12Model }, { "D20", &d20Model },
    { "Hex Prism", &hexPrismModel }, { "Drum", &drumModel }, { "Gem", &gemModel }, { "Boulder", &boulderModel }
  };

  for (const auto& shape : libraryShapes())
  {
    if (shape.name == name)
    {
      return shapeBody(name, *models.at(name), shape.hull);
    }
  }

  throw std::runtime_error("no library shape named " + name);
}

// Static structure: no RigidBody.
const json& rampBody()
{
  static const json body = makeObject("Ramp", json::array({
    transform(glm::vec3(0)),
    modelRenderer(rampModel, checkerBlueTexture, whiteTexture),
    convexMeshCollider(rampHull(), rampModel)
  }));

  return body;
}

const json& playerBody()
{
  static const json body = makeObject("Player", json::array({
    transform(glm::vec3(0)),
    modelRenderer(playerModel, whiteTexture, whiteTexture),
    rigidBody(sphereMass, playerFriction),
    sphereCollider(),
    playerController(),
    camera()
  }), json::array({
    {
      { "type", "Script" },
      { "className", "PlayerScript" },
      { "fields", json::array({
        { { "name", "m_topSpeed" }, { "type", "float" }, { "value", 9.0 } },
        { { "name", "m_jumpSpeed" }, { "type", "float" }, { "value", 15.0 } }
      })}
    }
  }));

  return body;
}

json prefabAsset(const std::string& name, const std::string& uuid, const json& body)
{
  return { { "name", name }, { "uuid", uuid }, { "body", body } };
}

// --- Instances ---------------------------------------------------------------------------------------
//
// The JSON-time equivalent of ObjectManager::instantiate: copy a prefab body, give it a fresh uuid, and
// override the root transform. (These bodies have no children; a body with children would need its whole
// subtree reassigned, which is what ObjectManager::reassignUUIDs does at runtime.)

json instanceOf(json body,
                const glm::vec3& position,
                const glm::vec3& scale,
                const glm::vec3& rotation)
{
  body["uuid"] = newUUID();

  for (auto& component : body.at("components"))
  {
    if (component.at("type") == "Transform")
    {
      component["position"] = vec(position);
      component["scale"] = vec(scale);
      component["rotation"] = vec(rotation);
      break;
    }
  }

  return body;
}

// An instance scaled away from its prefab keeps the prefab's density rather than its mass.
json withMass(json object, const float mass)
{
  for (auto& component : object.at("components"))
  {
    if (component.at("type") == "RigidBody")
    {
      component["mass"] = mass;
      break;
    }
  }

  return object;
}

json block(const glm::vec3& position,
           const glm::vec3& scale,
           const glm::vec3& rotation)
{
  return withMass(instanceOf(blockBody(), position, scale, rotation), blockMass * scale.x * scale.y * scale.z);
}

json rigidBlock(const glm::vec3& position,
                const glm::vec3& scale,
                const glm::vec3& rotation)
{
  return instanceOf(rigidBlockBody(), position, scale, rotation);
}

json sphere(const glm::vec3& position, const glm::vec3& scale)
{
  // A sphere collider's radius takes the largest scale axis.
  const float radius = std::max({ scale.x, scale.y, scale.z });

  return withMass(instanceOf(sphereBody(), position, scale), sphereMass * radius * radius * radius);
}

json player(const glm::vec3& position, const int slot,
            const glm::vec3& cameraDirection)
{
  // Per-instance overrides on top of the shared body: which player owns it, and where its camera looks.
  // The camera's facing lives on the Camera component, independent of the player model's orientation.
  auto object = instanceOf(playerBody(), position);
  object["name"] = "Player " + std::to_string(slot);

  for (auto& component : object.at("components"))
  {
    const auto& type = component.at("type");

    if (type == "PlayerController")
    {
      component["playerSlot"] = slot;
    }
    else if (type == "Camera")
    {
      component["direction"] = vec(cameraDirection);
    }
  }

  return object;
}

// Lights are parameterized well past what a prefab would capture (color/ambient/diffuse/specular all vary
// per instance), so they stay a plain builder rather than a prefab.
json light(const glm::vec3& position,
           const glm::vec3& color,
           const float ambient,
           const float diffuse,
           const float specular)
{
  return makeObject("Light", json::array({
    transform(position),
    {
      { "type", "LightRenderer" },
      { "color", vec(color) },
      { "direction", vec(glm::vec3(0, -1, 0)) },
      { "isSpotlight", false },
      { "ambient", ambient },
      { "diffuse", diffuse },
      { "specular", specular },
      { "coneAngle", 0.0 }
    }
  }));
}

// --- Scenes ----------------------------------------------------------------------------------------

json buildScene1()
{
  return json::array({
    light({ 0, 1.0f, 0 }, { 1.0f, 1.0f, 1.0f }, 0.25f, 0.0f, 0.0f),
    light({ -10, -0.375f, 3 }, { 0.0f, 1.0f, 1.0f }, 0.0f, 0.75f, 0.75f),
    light({ 10, -0.375f, 3 }, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.75f, 0.75f),
    rigidBlock({ 0, -10, 0 }, { 10, 1, 10 }),
    block({ 5, 5, 0 }),
    rigidBlock({ 15, -15, 0 }, { 10, 0.25f, 10 }, { 0, 0, 30 }),
    sphere({ 2, 0, 0 }),
    sphere({ 0, 2, 0 }),
    sphere({ 0, -2, 2 }),
    // Two players so an editor (slot 0) and a client (slot 1) each have their own camera POV when testing.
    // Each camera's direction aims back toward the scene (origin) from opposite sides.
    player({ 5, 0, 5 }, 0, glm::normalize(glm::vec3(-5, 0, -5))),
    player({ -5, 0, 5 }, 1, glm::normalize(glm::vec3(5, 0, -5)))
  });
}

json buildScene2()
{
  return json::array({
    light({ 0, 1.0f, 0 }, { 1.0f, 1.0f, 1.0f }, 0.25f, 0.0f, 0.0f),
    light({ -10, -0.375f, 3 }, { 0.0f, 1.0f, 1.0f }, 0.0f, 0.75f, 0.75f),
    light({ 10, -0.375f, 3 }, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.75f, 0.75f),
    rigidBlock({ 0, -10, 0 }, { 10, 1, 10 }),
    rigidBlock({ 18, -5, 0 }, { 10, 1, 10 }, { 0, 0, 30 }),
    rigidBlock({ -18, -5, 0 }, { 10, 1, 10 }, { 0, 0, -30 }),
    block({ -22, 10, -3 }),
    sphere({ 2, 0, 3 }),
    player({ 5, 0, 5 }, 0, glm::normalize(glm::vec3(-5, 0, -5)))
  });
}

// Procedurally-generated tower of random spheres/blocks.
json buildScene3()
{
  constexpr int gridSize = 6;
  constexpr int gridHeight = 15;
  constexpr int ballSpacing = 5;

  json objects = json::array({
    light({ 0, 1.0f, 0 }, { 1.0f, 1.0f, 1.0f }, 0.25f, 0.0f, 0.0f),
    light({ -25, 25.0f, 3 }, { 0.0f, 1.0f, 1.0f }, 0.0f, 0.75f, 0.75f),
    light({ 25, 25.0f, 3 }, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.75f, 0.75f),
    rigidBlock({ 0, -9, 0 }, { 100, 10, 100 })
  });

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dist(-0.75f, 0.75f);
  std::uniform_real_distribution<float> sphereSize(0.25f, 1.5f);
  std::uniform_int_distribution<int> shouldUseSphere(0, 1);
  std::uniform_real_distribution<float> rot(0.0f, 360.0f);

  for (int i = 0; i < gridHeight; i++)
  {
    for (int j = 0; j < gridSize; j++)
    {
      for (int k = 0; k < gridSize; k++)
      {
        constexpr float bottomY = 6.0f;
        constexpr float offsetXZ = (gridSize - 1) * ballSpacing / 2.0f;

        const float x = static_cast<float>(j) * ballSpacing + dist(gen) - offsetXZ;
        const float y = static_cast<float>(i) * ballSpacing + bottomY;
        const float z = static_cast<float>(k) * ballSpacing + dist(gen) - offsetXZ;

        if (shouldUseSphere(gen))
        {
          objects.push_back(sphere({ x, y, z }, glm::vec3(sphereSize(gen))));

          continue;
        }

        objects.push_back(block({ x, y, z },
          { sphereSize(gen), sphereSize(gen), sphereSize(gen) },
          { rot(gen), rot(gen), rot(gen) }));
      }
    }
  }

  return objects;
}

// Procedurally-generated grid of falling spheres.
json buildFallingBalls()
{
  constexpr int gridSize = 5;
  constexpr int gridHeight = 5;
  constexpr int ballSpacing = 3;

  json objects = json::array({
    light({ 0, 1.0f, 0 }, { 1.0f, 1.0f, 1.0f }, 0.25f, 0.0f, 0.0f),
    light({ -10, -0.375f, 3 }, { 0.0f, 1.0f, 1.0f }, 0.0f, 0.75f, 0.75f),
    light({ 10, -0.375f, 3 }, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.75f, 0.75f),
    rigidBlock({ 0, -15, 0 }, { 100, 3, 100 })
  });

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

  for (int i = 0; i < gridHeight; i++)
  {
    for (int j = 0; j < gridSize; j++)
    {
      for (int k = 0; k < gridSize; k++)
      {
        objects.push_back(sphere({
          static_cast<float>(j) * ballSpacing + dist(gen) - (gridSize * ballSpacing / 2.0f),
          static_cast<float>(i) * ballSpacing,
          static_cast<float>(k) * ballSpacing + dist(gen)
        }));
      }
    }
  }

  return objects;
}

json scene(const std::string& name, const std::string& uuid, json objects)
{
  return {
    { "name", name },
    { "objects", std::move(objects) },
    { "uuid", uuid }
  };
}

}

json buildDefaultProject()
{
  // Welcome is active out of the box; the rest are switchable from the editor's asset browser.
  const std::string welcomeUUID = newUUID();
  auto welcome = scene("Welcome", welcomeUUID, buildWelcomeObjects());
  welcome["scripts"] = buildWelcomeSceneScripts();

  return {
    { "assets", {
      { "models", json::array({
        { { "filePath", "assets/models/cube_1x1x1.glb" }, { "name", "assets/models/cube_1x1x1.glb" }, { "uuid", cubeModel } },
        { { "filePath", "assets/models/sphere.glb" }, { "name", "assets/models/sphere.glb" }, { "uuid", playerModel } },
        { { "filePath", "assets/models/sphere_2.glb" }, { "name", "assets/models/sphere_2.glb" }, { "uuid", newUUID() } },
        { { "filePath", "assets/models/sphere_3.glb" }, { "name", "assets/models/sphere_3.glb" }, { "uuid", sphereModel } },
        { { "filePath", "assets/models/square.glb" }, { "name", "assets/models/square.glb" }, { "uuid", newUUID() } },
        { { "filePath", "assets/models/capsule.glb" }, { "name", "assets/models/capsule.glb" }, { "uuid", capsuleModel } },
        { { "filePath", "assets/models/cylinder.glb" }, { "name", "assets/models/cylinder.glb" }, { "uuid", cylinderModel } },
        { { "filePath", "assets/models/ramp.glb" }, { "name", "assets/models/ramp.glb" }, { "uuid", rampModel } },
        { { "filePath", "assets/models/plane_tile.glb" }, { "name", "assets/models/plane_tile.glb" }, { "uuid", planeTileModel } },
        { { "filePath", "assets/models/torus.glb" }, { "name", "assets/models/torus.glb" }, { "uuid", torusModel } },
        { { "filePath", "assets/models/d4.glb" }, { "name", "assets/models/d4.glb" }, { "uuid", d4Model } },
        { { "filePath", "assets/models/d8.glb" }, { "name", "assets/models/d8.glb" }, { "uuid", d8Model } },
        { { "filePath", "assets/models/d12.glb" }, { "name", "assets/models/d12.glb" }, { "uuid", d12Model } },
        { { "filePath", "assets/models/d20.glb" }, { "name", "assets/models/d20.glb" }, { "uuid", d20Model } },
        { { "filePath", "assets/models/hex_prism.glb" }, { "name", "assets/models/hex_prism.glb" }, { "uuid", hexPrismModel } },
        { { "filePath", "assets/models/drum.glb" }, { "name", "assets/models/drum.glb" }, { "uuid", drumModel } },
        { { "filePath", "assets/models/gem.glb" }, { "name", "assets/models/gem.glb" }, { "uuid", gemModel } },
        { { "filePath", "assets/models/boulder.glb" }, { "name", "assets/models/boulder.glb" }, { "uuid", boulderModel } }
      })},
      { "textures", json::array({
        { { "filePath", "assets/textures/black.png" }, { "name", "assets/textures/black.png" }, { "uuid", newUUID() } },
        { { "filePath", "assets/textures/white.png" }, { "name", "assets/textures/white.png" }, { "uuid", whiteTexture } },
        { { "filePath", "assets/textures/earth.png" }, { "name", "assets/textures/earth.png" }, { "uuid", earthTexture } },
        { { "filePath", "assets/textures/earth_specular.png" }, { "name", "assets/textures/earth_specular.png" }, { "uuid", earthSpecularTexture } },
        { { "filePath", "assets/textures/grid_checker.png" }, { "name", "assets/textures/grid_checker.png" }, { "uuid", gridCheckerTexture } },
        { { "filePath", "assets/textures/checker_orange.png" }, { "name", "assets/textures/checker_orange.png" }, { "uuid", checkerOrangeTexture } },
        { { "filePath", "assets/textures/checker_blue.png" }, { "name", "assets/textures/checker_blue.png" }, { "uuid", checkerBlueTexture } },
        { { "filePath", "assets/textures/palette_red.png" }, { "name", "assets/textures/palette_red.png" }, { "uuid", paletteRedTexture } },
        { { "filePath", "assets/textures/palette_green.png" }, { "name", "assets/textures/palette_green.png" }, { "uuid", paletteGreenTexture } },
        { { "filePath", "assets/textures/palette_blue.png" }, { "name", "assets/textures/palette_blue.png" }, { "uuid", paletteBlueTexture } },
        { { "filePath", "assets/textures/palette_yellow.png" }, { "name", "assets/textures/palette_yellow.png" }, { "uuid", paletteYellowTexture } },
        { { "filePath", "assets/textures/palette_orange.png" }, { "name", "assets/textures/palette_orange.png" }, { "uuid", paletteOrangeTexture } },
        { { "filePath", "assets/textures/palette_purple.png" }, { "name", "assets/textures/palette_purple.png" }, { "uuid", palettePurpleTexture } }
      })},
      { "scripts", json::array({
        { { "className", "BlockScript" }, { "filePath", "scripts/UserScripts/BlockScript.cs" }, { "uuid", newUUID() } },
        { { "className", "PlayerScript" }, { "filePath", "scripts/UserScripts/PlayerScript.cs" }, { "uuid", playerScript } }
      })},
      // The same bodies the scenes below instantiate, exposed as assets: drag one into the scene from the
      // editor's asset browser, or spawn it from a script with World.spawnPrefab(uuid, position).
      { "prefabs", json::array({
        prefabAsset("Block", blockPrefab, blockBody()),
        prefabAsset("Rigid Block", rigidBlockPrefab, rigidBlockBody()),
        prefabAsset("Sphere", spherePrefab, sphereBody()),
        prefabAsset("Player", playerPrefab, playerBody()),
        prefabAsset("Capsule", capsulePrefab, capsuleBody()),
        prefabAsset("Cylinder", cylinderPrefab, cylinderBody()),
        prefabAsset("Ramp", rampPrefab, rampBody()),
        prefabAsset("D4", d4Prefab, shapeBody("D4", d4Model, d4Hull)),
        prefabAsset("D6", d6Prefab, shapeBody("D6", cubeModel, d6Hull)),
        prefabAsset("D8", d8Prefab, shapeBody("D8", d8Model, d8Hull)),
        prefabAsset("D12", d12Prefab, shapeBody("D12", d12Model, d12Hull)),
        prefabAsset("D20", d20Prefab, shapeBody("D20", d20Model, d20Hull)),
        prefabAsset("Hex Prism", hexPrismPrefab, shapeBody("Hex Prism", hexPrismModel, hexPrismHull)),
        prefabAsset("Drum", drumPrefab, shapeBody("Drum", drumModel, drumHull)),
        prefabAsset("Gem", gemPrefab, shapeBody("Gem", gemModel, gemHull)),
        prefabAsset("Boulder", boulderPrefab, shapeBody("Boulder", boulderModel, boulderHull))
      })},
      { "scenes", json::array({
        std::move(welcome),
        scene("Lights and Players", newUUID(), buildScene1()),
        scene("Ramps", newUUID(), buildScene2()),
        scene("Tower Collapse", newUUID(), buildScene3()),
        scene("Falling Balls", newUUID(), buildFallingBalls())
      })}
    }},
    { "currentSceneUUID", welcomeUUID }
  };
}
