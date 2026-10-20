#include "DefaultProject.h"
#include "DefaultShapes.h"
#include <nlohmann/json.hpp>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <map>
#include <numbers>
#include <random>
#include <string>
#include <uuid.h>

using nlohmann::json;

namespace {

// Stable asset UUIDs for the shared models/textures/script. Object UUIDs are freshly generated each launch (newUUID).
const std::string cubeModel = "58034695-0495-4cfb-8f75-90cf24a9073b";
const std::string playerModel = "bcf01ddf-e82a-4ed1-90a7-b93b4184f647";
const std::string sphereModel = "a4c4a30b-735b-4b1c-9140-19f62ca3c720";
const std::string whiteTexture = "a3628d76-f753-4e27-be83-35744d1ac44d";
const std::string earthTexture = "2cfcc1b3-86f7-487b-bf37-6255111ab0ca";
const std::string earthSpecularTexture = "9e4b3267-a29c-43ac-aca4-ccd06f078f5c";
const std::string playerScript = "de114693-8cf3-4123-a308-70dbb2af120f";
const std::string capsuleModel = "953f4317-65aa-4eb0-b186-c0b94bec185e";
const std::string rampModel = "e77194e0-e50e-4070-aacd-2f5d917b2da7";
const std::string planeTileModel = "6a742837-aa60-43fe-a652-32a3d385cde0";
const std::string torusModel = "7a1cd753-3775-44a0-858f-27239f8740f5";
const std::string cylinderModel = "a383cc8f-ee6e-4373-ae06-f10f85ef159b";
const std::string d4Model = "4e4e2499-ff78-43d8-998e-c7cfc149337e";
const std::string d8Model = "6539110c-cfb2-4d1e-8888-6dcefc76a66b";
const std::string d12Model = "5f4873d6-d2c7-4313-b232-728b949f1dd6";
const std::string d20Model = "f810cee9-5c0b-47f6-9943-ae185c0105bd";
const std::string hexPrismModel = "dc62625e-9c71-4de3-9a1d-56e6d7ba47c0";
const std::string drumModel = "75fac0ef-08ca-4c20-81fa-de5aa9269d50";
const std::string gemModel = "ad754062-1662-4446-8aac-aefa926d49b6";
const std::string boulderModel = "9d0e47fd-8a6f-4ff9-9614-ddb0c5dddd5a";
const std::string gridCheckerTexture = "48ad67dd-957f-4fc9-bd7b-019debffa1d0";
const std::string checkerOrangeTexture = "425f3784-74eb-4e09-bfbb-88facfb014d0";
const std::string checkerBlueTexture = "05325ffd-75bd-4d76-a50d-7b80e183b081";
const std::string paletteRedTexture = "7c8c01db-771f-4048-94c2-004b8d19e7aa";
const std::string paletteGreenTexture = "c5eee5e6-9bd0-4f9b-977f-b9220a21a630";
const std::string paletteBlueTexture = "66a8a6fc-d151-4567-9ce5-2e17cc675fe0";
const std::string paletteYellowTexture = "1d5edebf-57f9-499c-9840-b84a57174422";
const std::string paletteOrangeTexture = "ea184ad8-ca64-48cd-b69e-92bcb4649158";
const std::string palettePurpleTexture = "8925b024-c923-4839-9e6b-fa67e69e6752";

// Stable prefab UUIDs, so a script can spawn one by a uuid it can hardcode (World.spawnPrefab).
const std::string blockPrefab = "0a1e5c2d-7b3f-4a91-8c60-1d4e2f6a8b03";
const std::string rigidBlockPrefab = "3f8c1a47-92d5-4e08-b1a6-5c7e09f4d2b1";
const std::string spherePrefab = "6d2b4f19-08ac-4d73-9e52-b83a1c6f0e47";
const std::string playerPrefab = "9c7f0e83-4d16-4b52-a09e-27f5b3d81c6a";
const std::string capsulePrefab = "39e415e0-ff7a-475c-b677-72062ce2954a";
const std::string cylinderPrefab = "62f97ec2-a58e-4e6d-b9e4-a466dc04669a";
const std::string rampPrefab = "1b112469-4331-402e-bc4a-94cd0368ef52";
const std::string d4Prefab = "d973cf42-3ef2-4ea6-a4a4-e505fa2d0c17";
const std::string d6Prefab = "f02de30b-fa89-400f-b2ba-fbabcd219775";
const std::string d8Prefab = "fab33a9c-0c55-488f-b5ab-516cc7558075";
const std::string d12Prefab = "c901eb04-d6b1-480b-a556-19f9fba037c7";
const std::string d20Prefab = "32cf047d-3fa4-40a5-9dbd-5377aa5bbaa1";
const std::string hexPrismPrefab = "cb56959b-90d6-4b14-b2bb-64bc5e3b08f1";
const std::string drumPrefab = "41980fde-c3d9-4672-87a3-71d80acdc734";
const std::string gemPrefab = "98f4b839-f256-4562-b8ff-266094c98725";
const std::string boulderPrefab = "dca2e8d6-7dab-4706-84f2-61da7ad076f1";

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
               const glm::vec3& scale = glm::vec3(1),
               const glm::vec3& rotation = glm::vec3(0))
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

json makeObject(const std::string& name, json components, json scripts = json::array())
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
                const glm::vec3& scale = glm::vec3(1),
                const glm::vec3& rotation = glm::vec3(0))
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
           const glm::vec3& scale = glm::vec3(1),
           const glm::vec3& rotation = glm::vec3(0))
{
  return withMass(instanceOf(blockBody(), position, scale, rotation), blockMass * scale.x * scale.y * scale.z);
}

json rigidBlock(const glm::vec3& position,
                const glm::vec3& scale,
                const glm::vec3& rotation = glm::vec3(0))
{
  return instanceOf(rigidBlockBody(), position, scale, rotation);
}

json sphere(const glm::vec3& position, const glm::vec3& scale = glm::vec3(1))
{
  // A sphere collider's radius takes the largest scale axis.
  const float radius = std::max({ scale.x, scale.y, scale.z });

  return withMass(instanceOf(sphereBody(), position, scale), sphereMass * radius * radius * radius);
}

json player(const glm::vec3& position, const int slot = 0,
            const glm::vec3& cameraDirection = glm::vec3(0, 0, -1))
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
  // Scene 1 is active out of the box; the rest are switchable from the editor's asset browser.
  const std::string scene1UUID = newUUID();

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
        scene("Scene 1", scene1UUID, buildScene1()),
        scene("Scene 2", newUUID(), buildScene2()),
        scene("Scene 3", newUUID(), buildScene3()),
        scene("Falling Balls", newUUID(), buildFallingBalls())
      })}
    }},
    { "currentSceneUUID", scene1UUID }
  };
}
