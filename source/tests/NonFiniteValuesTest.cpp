#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "ProjectSerializer.h"
#include "TestScene.h"
#include "WireTypes.h"
#include "objects/components/FiniteCheck.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include "objects/components/Camera.h"
#include "objects/components/LightRenderer.h"
#include "objects/components/ModelRenderer.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/CapsuleCollider.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <Protocol.h>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <uuid.h>

namespace {
  constexpr float infinity = std::numeric_limits<float>::infinity();

  const float nonFiniteValues[] = { infinity, -infinity, std::numeric_limits<float>::quiet_NaN() };

  // Every guarded setter is checked the same way: store a finite value first - the positive control,
  // without which a setter that ignored everything would pass - then push each non-finite value at it
  // and require the finite one to still read back.
  void expectFloatSetterIgnoresNonFinite(const char* what, const float finiteValue,
                                         const std::function<void(float)>& set,
                                         const std::function<float()>& get)
  {
    set(finiteValue);
    EXPECT_FLOAT_EQ(get(), finiteValue) << what;

    for (const float value : nonFiniteValues)
    {
      set(value);
      EXPECT_FLOAT_EQ(get(), finiteValue) << what;
    }
  }

  // One component non-finite at a time: a setter that only looked at x would otherwise pass.
  void expectVec3SetterIgnoresNonFinite(const char* what,
                                        const std::function<void(const glm::vec3&)>& set,
                                        const std::function<glm::vec3()>& get)
  {
    const glm::vec3 finiteValue(1.5f, -2.25f, 3.75f);

    set(finiteValue);
    fixtures::expectNear(what, get(), finiteValue);

    for (const float value : nonFiniteValues)
    {
      for (int axis = 0; axis < 3; ++axis)
      {
        glm::vec3 candidate = finiteValue;
        candidate[axis] = value;

        set(candidate);
        fixtures::expectNear(what, get(), finiteValue);
      }
    }
  }

  // The payload RigidBody::pack writes, with every field the caller's own so one can be non-finite.
  struct RigidBodyFields {
    glm::vec3 velocity{ 1.0f, 2.0f, 3.0f };
    float friction = 0.25f;
    bool doGravity = false;
    float gravity = -4.5f;
    glm::vec3 angularVelocity{ -1.0f, 0.5f, 2.0f };
    float mass = 7.5f;
  };

  net::Message packedRigidBody(const RigidBodyFields& fields)
  {
    net::Message message(net::MessageType::undefined);
    message.write(ComponentType::rigidBody);
    message.write(fields.velocity);
    message.write(fields.friction);
    message.write(fields.doGravity);
    message.write(fields.gravity);
    message.write(fields.angularVelocity);
    message.write(fields.mass);
    return message;
  }

  void unpackRigidBody(RigidBody& rigidBody, const RigidBodyFields& fields)
  {
    const auto message = packedRigidBody(fields);
    net::MessageReader reader(message);
    // pack writes the type discriminator first; unpack expects the reader positioned after it.
    static_cast<void>(reader.read<ComponentType>());
    rigidBody.unpack(reader);
  }

  // The value a field holds after a payload: the payload's own when finite, otherwise the earlier one.
  float expectedAfter(const float incoming, const float previous)
  {
    return std::isfinite(incoming) ? incoming : previous;
  }

  glm::vec3 expectedAfter(const glm::vec3& incoming, const glm::vec3& previous)
  {
    return finiteCheck::isFinite(incoming) ? incoming : previous;
  }

  void expectFieldsAfterUnpack(const char* what, const RigidBody& rigidBody, const RigidBodyFields& previous,
                               const RigidBodyFields& incoming)
  {
    fixtures::expectNear(what, rigidBody.getVelocity(), expectedAfter(incoming.velocity, previous.velocity));
    fixtures::expectNear(what, rigidBody.getAngularVelocity(),
                         expectedAfter(incoming.angularVelocity, previous.angularVelocity));
    EXPECT_FLOAT_EQ(rigidBody.getFriction(), expectedAfter(incoming.friction, previous.friction)) << what;
    EXPECT_FLOAT_EQ(rigidBody.getGravity(), expectedAfter(incoming.gravity, previous.gravity)) << what;
    EXPECT_FLOAT_EQ(rigidBody.getMass(), expectedAfter(incoming.mass, previous.mass)) << what;
    EXPECT_EQ(rigidBody.getDoGravity(), incoming.doGravity) << what;
  }

  RigidBodyFields secondPayload()
  {
    RigidBodyFields next;
    next.velocity = { 9.0f, 9.0f, 9.0f };
    next.friction = 0.9f;
    next.doGravity = true;
    next.gravity = -1.0f;
    next.angularVelocity = { 8.0f, 8.0f, 8.0f };
    next.mass = 20.0f;
    return next;
  }

  // Applies a finite payload first - the positive control - then one whose field is non-finite, and requires
  // the corrupted field to keep the earlier value while every other field takes the new payload, which also
  // pins that the reader stayed aligned past the dropped value.
  void expectUnpackDropsNonFinite(const char* what, const std::function<void(RigidBodyFields&, float)>& corrupt)
  {
    for (const float value : nonFiniteValues)
    {
      RigidBody rigidBody;
      const RigidBodyFields finiteFields;
      unpackRigidBody(rigidBody, finiteFields);
      expectFieldsAfterUnpack(what, rigidBody, finiteFields, finiteFields);

      RigidBodyFields next = secondPayload();
      corrupt(next, value);
      unpackRigidBody(rigidBody, next);
      expectFieldsAfterUnpack(what, rigidBody, finiteFields, next);
    }
  }

  struct Project {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    std::unique_ptr<AssetRegistry> assetRegistry = std::make_unique<AssetRegistry>();
    std::unique_ptr<SceneManager> sceneManager = std::make_unique<SceneManager>();
    std::unique_ptr<ProjectSerializer> serializer;
  };

  Project makeProject()
  {
    Project project;
    registerDataComponents(*project.componentRegistry);

    project.serializer = std::make_unique<ProjectSerializer>(project.assetRegistry.get(),
                                                             project.sceneManager.get(),
                                                             project.componentRegistry);

    return project;
  }

  // One scene holding one object: the smallest project with a Transform position to break.
  nlohmann::json buildProject(const Project& project)
  {
    const auto uuid = uuids::uuid::from_string("66666666-6666-6666-6666-666666666666").value();
    const auto scene = std::make_shared<SceneAsset>(uuid, "Main", project.componentRegistry);
    project.sceneManager->addScene(scene);

    scene->getObjectManager()->addObject(std::make_shared<Object>("Body"));

    return project.serializer->serialize();
  }
}

TEST(NonFiniteValues, TransformSettersIgnoreThem)
{
  Transform transform;

  // getLocal*, not the parent-combined getters: a bare component has no owning object to walk to.
  expectVec3SetterIgnoresNonFinite("Transform position",
    [&](const glm::vec3& value) { transform.setPosition(value); },
    [&] { return transform.getLocalPosition(); });

  expectVec3SetterIgnoresNonFinite("Transform rotation",
    [&](const glm::vec3& value) { transform.setRotation(value); },
    [&] { return transform.getLocalRotation(); });

  expectVec3SetterIgnoresNonFinite("Transform scale",
    [&](const glm::vec3& value) { transform.setScale(value); },
    [&] { return transform.getLocalScale(); });
}

TEST(NonFiniteValues, BoxColliderSettersIgnoreThem)
{
  BoxCollider collider;

  expectVec3SetterIgnoresNonFinite("BoxCollider position",
    [&](const glm::vec3& value) { collider.setPosition(value); },
    [&] { return collider.getLocalPosition(); });

  expectVec3SetterIgnoresNonFinite("BoxCollider rotation",
    [&](const glm::vec3& value) { collider.setRotation(value); },
    [&] { return collider.getLocalRotation(); });

  expectVec3SetterIgnoresNonFinite("BoxCollider scale",
    [&](const glm::vec3& value) { collider.setScale(value); },
    [&] { return collider.getLocalScale(); });
}

TEST(NonFiniteValues, SphereColliderSettersIgnoreThem)
{
  SphereCollider collider;

  expectFloatSetterIgnoresNonFinite("SphereCollider radius", 2.5f,
    [&](const float value) { collider.setRadius(value); },
    [&] { return collider.getLocalRadius(); });

  expectVec3SetterIgnoresNonFinite("SphereCollider position",
    [&](const glm::vec3& value) { collider.setPosition(value); },
    [&] { return collider.getLocalPosition(); });
}

TEST(NonFiniteValues, CapsuleColliderSettersIgnoreThem)
{
  CapsuleCollider collider;

  expectFloatSetterIgnoresNonFinite("CapsuleCollider radius", 2.5f,
    [&](const float value) { collider.setRadius(value); },
    [&] { return collider.getLocalRadius(); });

  expectFloatSetterIgnoresNonFinite("CapsuleCollider height", 4.5f,
    [&](const float value) { collider.setHeight(value); },
    [&] { return collider.getLocalHeight(); });

  expectVec3SetterIgnoresNonFinite("CapsuleCollider position",
    [&](const glm::vec3& value) { collider.setPosition(value); },
    [&] { return collider.getLocalPosition(); });

  expectVec3SetterIgnoresNonFinite("CapsuleCollider rotation",
    [&](const glm::vec3& value) { collider.setRotation(value); },
    [&] { return collider.getLocalRotation(); });
}

TEST(NonFiniteValues, RigidBodySettersIgnoreThem)
{
  RigidBody rigidBody;

  expectVec3SetterIgnoresNonFinite("RigidBody velocity",
    [&](const glm::vec3& value) { rigidBody.setVelocity(value); },
    [&] { return rigidBody.getVelocity(); });

  expectVec3SetterIgnoresNonFinite("RigidBody angular velocity",
    [&](const glm::vec3& value) { rigidBody.setAngularVelocity(value); },
    [&] { return rigidBody.getAngularVelocity(); });

  expectFloatSetterIgnoresNonFinite("RigidBody mass", 12.5f,
    [&](const float value) { rigidBody.setMass(value); },
    [&] { return rigidBody.getMass(); });

  expectFloatSetterIgnoresNonFinite("RigidBody friction", 0.4f,
    [&](const float value) { rigidBody.setFriction(value); },
    [&] { return rigidBody.getFriction(); });

  expectFloatSetterIgnoresNonFinite("RigidBody gravity", -9.81f,
    [&](const float value) { rigidBody.setGravity(value); },
    [&] { return rigidBody.getGravity(); });
}

TEST(NonFiniteValues, RigidBodyUnpackDropsANonFiniteVelocity)
{
  expectUnpackDropsNonFinite("velocity x", [](RigidBodyFields& fields, const float value) { fields.velocity.x = value; });
  expectUnpackDropsNonFinite("velocity z", [](RigidBodyFields& fields, const float value) { fields.velocity.z = value; });
}

TEST(NonFiniteValues, RigidBodyUnpackDropsANonFiniteAngularVelocity)
{
  expectUnpackDropsNonFinite("angular velocity y",
    [](RigidBodyFields& fields, const float value) { fields.angularVelocity.y = value; });
}

TEST(NonFiniteValues, RigidBodyUnpackDropsANonFiniteFriction)
{
  expectUnpackDropsNonFinite("friction", [](RigidBodyFields& fields, const float value) { fields.friction = value; });
}

TEST(NonFiniteValues, RigidBodyUnpackDropsANonFiniteGravity)
{
  expectUnpackDropsNonFinite("gravity", [](RigidBodyFields& fields, const float value) { fields.gravity = value; });
}

TEST(NonFiniteValues, RigidBodyUnpackDropsANonFiniteMass)
{
  expectUnpackDropsNonFinite("mass", [](RigidBodyFields& fields, const float value) { fields.mass = value; });
}

TEST(NonFiniteValues, RigidBodyLoadFromJsonDropsNonFiniteInsteadOfThrowing)
{
  RigidBody rigidBody;
  rigidBody.setVelocity({ 1.0f, 2.0f, 3.0f });
  rigidBody.setAngularVelocity({ 4.0f, 5.0f, 6.0f });
  rigidBody.setFriction(0.25f);
  rigidBody.setGravity(-3.0f);
  rigidBody.setMass(8.0f);
  rigidBody.setDoGravity(false);

  // A non-finite float saved as json null: one element of each vec3 and every scalar.
  const nlohmann::json data = {
    { "type", "RigidBody" },
    { "velocity", { nullptr, 5.0f, 6.0f } },
    { "angularVelocity", { 0.0f, nullptr, 4.0f } },
    { "friction", nullptr },
    { "doGravity", true },
    { "gravity", nullptr },
    { "mass", nullptr }
  };

  EXPECT_NO_THROW(rigidBody.loadFromJSON(data));

  fixtures::expectNear("velocity", rigidBody.getVelocity(), glm::vec3(1.0f, 2.0f, 3.0f));
  fixtures::expectNear("angular velocity", rigidBody.getAngularVelocity(), glm::vec3(4.0f, 5.0f, 6.0f));
  EXPECT_FLOAT_EQ(rigidBody.getFriction(), 0.25f);
  EXPECT_FLOAT_EQ(rigidBody.getGravity(), -3.0f);
  EXPECT_FLOAT_EQ(rigidBody.getMass(), 8.0f);
  // Positive control: the finite field of the same document was applied.
  EXPECT_TRUE(rigidBody.getDoGravity());

  const nlohmann::json finiteData = {
    { "type", "RigidBody" },
    { "velocity", { 9.0f, 5.0f, 6.0f } },
    { "angularVelocity", { 0.0f, 7.0f, 4.0f } },
    { "friction", 0.75f },
    { "doGravity", true },
    { "gravity", -1.5f },
    { "mass", 3.0f }
  };

  rigidBody.loadFromJSON(finiteData);

  fixtures::expectNear("velocity", rigidBody.getVelocity(), glm::vec3(9.0f, 5.0f, 6.0f));
  fixtures::expectNear("angular velocity", rigidBody.getAngularVelocity(), glm::vec3(0.0f, 7.0f, 4.0f));
  EXPECT_FLOAT_EQ(rigidBody.getFriction(), 0.75f);
  EXPECT_FLOAT_EQ(rigidBody.getGravity(), -1.5f);
  EXPECT_FLOAT_EQ(rigidBody.getMass(), 3.0f);
}

TEST(NonFiniteValues, RigidBodyLoadFromJsonStillThrowsOnAWrongShape)
{
  const nlohmann::json valid = {
    { "type", "RigidBody" },
    { "velocity", { 1.0f, 2.0f, 3.0f } },
    { "angularVelocity", { 0.0f, 0.0f, 0.0f } },
    { "friction", 0.5f },
    { "doGravity", true },
    { "gravity", -9.81f },
    { "mass", 10.0f }
  };

  RigidBody control;
  EXPECT_NO_THROW(control.loadFromJSON(valid));

  auto notAnArray = valid;
  notAnArray["velocity"] = 5.0f;
  RigidBody shaped;
  EXPECT_THROW(shaped.loadFromJSON(notAnArray), nlohmann::json::exception);

  auto missingKey = valid;
  missingKey.erase("mass");
  RigidBody missing;
  EXPECT_THROW(missing.loadFromJSON(missingKey), nlohmann::json::exception);
}

TEST(NonFiniteValues, CameraSettersIgnoreThem)
{
  Camera camera;

  expectVec3SetterIgnoresNonFinite("Camera direction",
    [&](const glm::vec3& value) { camera.setDirection(value); },
    [&] { return camera.getDirection(); });

  expectFloatSetterIgnoresNonFinite("Camera fov", 70.0f,
    [&](const float value) { camera.setFov(value); },
    [&] { return camera.getFov(); });

  expectFloatSetterIgnoresNonFinite("Camera near plane", 0.05f,
    [&](const float value) { camera.setNearPlane(value); },
    [&] { return camera.getNearPlane(); });

  expectFloatSetterIgnoresNonFinite("Camera far plane", 500.0f,
    [&](const float value) { camera.setFarPlane(value); },
    [&] { return camera.getFarPlane(); });
}

TEST(NonFiniteValues, LightRendererSettersIgnoreThem)
{
  LightRenderer light;

  expectVec3SetterIgnoresNonFinite("LightRenderer color",
    [&](const glm::vec3& value) { light.setColor(value); },
    [&] { return light.getColor(); });

  expectVec3SetterIgnoresNonFinite("LightRenderer direction",
    [&](const glm::vec3& value) { light.setDirection(value); },
    [&] { return light.getDirection(); });

  expectFloatSetterIgnoresNonFinite("LightRenderer ambient", 0.1f,
    [&](const float value) { light.setAmbient(value); },
    [&] { return light.getAmbient(); });

  expectFloatSetterIgnoresNonFinite("LightRenderer diffuse", 0.6f,
    [&](const float value) { light.setDiffuse(value); },
    [&] { return light.getDiffuse(); });

  expectFloatSetterIgnoresNonFinite("LightRenderer specular", 0.3f,
    [&](const float value) { light.setSpecular(value); },
    [&] { return light.getSpecular(); });

  // Inside the existing clamp range, so the control value survives the clamp unchanged and the failure
  // this asserts is the non-finite one rather than the clamping.
  expectFloatSetterIgnoresNonFinite("LightRenderer cone angle", 25.0f,
    [&](const float value) { light.setConeAngle(value); },
    [&] { return light.getConeAngle(); });
}

TEST(NonFiniteValues, ModelRendererSettersIgnoreThem)
{
  ModelRenderer modelRenderer;

  expectFloatSetterIgnoresNonFinite("ModelRenderer reflectivity", 0.75f,
    [&](const float value) { modelRenderer.setReflectivity(value); },
    [&] { return modelRenderer.getReflectivity(); });
}

TEST(NonFiniteValues, ARefusedSetLeavesNoNullInTheSerializedObject)
{
  const auto scene = fixtures::makeScene();
  const auto object = fixtures::addObject(scene, "Body");

  const auto transform = fixtures::transformOf(object);
  transform->setPosition({ 1.5f, -2.0f, 3.25f });
  transform->setPosition({ infinity, 0.0f, 0.0f });

  const auto data = object->serialize();

  // json has no non-finite number, so it dumps one as the literal null - and loading that back is what
  // throws the whole project file away.
  const auto dumped = data.dump();
  EXPECT_EQ(dumped.find("null"), std::string::npos) << dumped;

  const nlohmann::json* transformData = nullptr;
  for (const auto& component : data.at("components"))
  {
    if (component.at("type") == "Transform")
    {
      transformData = &component;
      break;
    }
  }

  ASSERT_NE(transformData, nullptr);
  EXPECT_FLOAT_EQ(transformData->at("position").at(0).get<float>(), 1.5f);
  EXPECT_FLOAT_EQ(transformData->at("position").at(1).get<float>(), -2.0f);
  EXPECT_FLOAT_EQ(transformData->at("position").at(2).get<float>(), 3.25f);
}

TEST(NonFiniteValues, AFailedLoadNamesTheSceneTheObjectAndTheComponent)
{
  const auto source = makeProject();
  const auto blob = buildProject(source);

  auto broken = blob;
  bool injected = false;

  for (auto& scene : broken.at("assets").at("scenes"))
  {
    for (auto& object : scene.at("objects"))
    {
      for (auto& component : object.at("components"))
      {
        if (component.at("type") == "Transform")
        {
          // A null float now loads as a rejected value, so a missing required key is what breaks the load.
          component.erase("position");
          injected = true;
          break;
        }
      }
    }
  }

  ASSERT_TRUE(injected);

  // Positive control: the same blob without the null loads, so the throw below is about the missing key rather
  // than about a blob that was never loadable.
  const auto control = makeProject();
  EXPECT_NO_THROW(control.serializer->deserialize(blob));
  EXPECT_EQ(control.sceneManager->getScenes().size(), 1u);

  const auto target = makeProject();
  const auto before = buildProject(target);

  try
  {
    target.serializer->deserialize(broken);
    FAIL() << "a Transform with no position must not load";
  }
  catch (const std::runtime_error& error)
  {
    // The diagnostic is the point: without these the log line is an exception text with no way to tell
    // which of a project's scenes and objects to go and fix.
    const std::string message = error.what();
    EXPECT_NE(message.find("Main"), std::string::npos) << message;
    EXPECT_NE(message.find("Body"), std::string::npos) << message;
    EXPECT_NE(message.find("Transform"), std::string::npos) << message;
  }

  // Still atomic: the failed load must leave the live scenes and assets exactly as they were.
  EXPECT_EQ(target.serializer->serialize(), before);
}

TEST(NonFiniteValues, ConvexMeshColliderSettersIgnoreThem)
{
  ConvexMeshCollider collider;

  expectVec3SetterIgnoresNonFinite("ConvexMeshCollider position",
    [&](const glm::vec3& value) { collider.setPosition(value); },
    [&] { return collider.getLocalPosition(); });

  expectVec3SetterIgnoresNonFinite("ConvexMeshCollider rotation",
    [&](const glm::vec3& value) { collider.setRotation(value); },
    [&] { return collider.getLocalRotation(); });

  expectVec3SetterIgnoresNonFinite("ConvexMeshCollider scale",
    [&](const glm::vec3& value) { collider.setScale(value); },
    [&] { return collider.getLocalScale(); });
}
