#include "DefaultProject.h"
#include <nlohmann/json.hpp>
#include <glm/vec3.hpp>
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

json rigidBody()
{
  return {
    { "type", "RigidBody" },
    { "velocity", vec(glm::vec3(0)) },
    { "angularVelocity", vec(glm::vec3(0)) },
    { "friction", 0.1 },
    { "doGravity", true },
    { "gravity", -9.81 },
    { "mass", 10.0 }
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
    { "renderCollider", false },
    { "position", vec(glm::vec3(0)) }
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

// --- Prefabs (mirror tests/common/Prefabs.h) -------------------------------------------------------

json block(const glm::vec3& position,
           const glm::vec3& scale = glm::vec3(1),
           const glm::vec3& rotation = glm::vec3(0))
{
  return makeObject("Block", json::array({
    transform(position, scale, rotation),
    modelRenderer(cubeModel, whiteTexture, whiteTexture),
    rigidBody(),
    boxCollider()
  }));
}

json rigidBlock(const glm::vec3& position,
                const glm::vec3& scale,
                const glm::vec3& rotation = glm::vec3(0))
{
  return makeObject("Rigid Block", json::array({
    transform(position, scale, rotation),
    modelRenderer(cubeModel, whiteTexture, whiteTexture),
    boxCollider()
  }));
}

json sphere(const glm::vec3& position, const glm::vec3& scale = glm::vec3(1))
{
  return makeObject("Sphere", json::array({
    transform(position, scale),
    modelRenderer(sphereModel, earthTexture, earthSpecularTexture),
    rigidBody(),
    sphereCollider()
  }));
}

json player(const glm::vec3& position)
{
  return makeObject("Player", json::array({
    transform(position),
    modelRenderer(playerModel, whiteTexture, whiteTexture),
    rigidBody(),
    sphereCollider()
  }), json::array({
    {
      { "type", "Script" },
      { "className", "PlayerScript" },
      { "fields", json::array({
        { { "name", "m_speed" }, { "type", "float" }, { "value", 1.0 } },
        { { "name", "m_jumpForce" }, { "type", "float" }, { "value", 15.0 } }
      })}
    }
  }));
}

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
    player({ 5, 0, 5 })
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
    player({ 5, 0, 5 })
  });
}

// Procedurally-generated tower of random spheres/blocks (was setup test loadScene3).
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

// Procedurally-generated grid of falling spheres (was the fallingBalls test).
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
        { { "filePath", "assets/models/sphere_3.glb" }, { "name", "assets/models/sphere_3.glb" }, { "uuid", sphereModel } }
      })},
      { "textures", json::array({
        { { "filePath", "assets/textures/white.png" }, { "name", "assets/textures/white.png" }, { "uuid", whiteTexture } },
        { { "filePath", "assets/textures/earth.png" }, { "name", "assets/textures/earth.png" }, { "uuid", earthTexture } },
        { { "filePath", "assets/textures/earth_specular.png" }, { "name", "assets/textures/earth_specular.png" }, { "uuid", earthSpecularTexture } }
      })},
      { "scripts", json::array({
        { { "className", "PlayerScript" }, { "filePath", "scripts/UserScripts/PlayerScript.cs" }, { "uuid", playerScript } }
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
