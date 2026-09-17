#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "ProjectSerializer.h"
#include "TestScene.h"
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
#include "objects/components/collisions/SphereCollider.h"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

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
          // Exactly what dump() writes where a non-finite float was stored.
          component.at("position").at(0) = nullptr;
          injected = true;
          break;
        }
      }
    }
  }

  ASSERT_TRUE(injected);

  // Positive control: the same blob without the null loads, so the throw below is about the null rather
  // than about a blob that was never loadable.
  const auto control = makeProject();
  EXPECT_NO_THROW(control.serializer->deserialize(blob));
  EXPECT_EQ(control.sceneManager->getScenes().size(), 1u);

  const auto target = makeProject();
  const auto before = buildProject(target);

  try
  {
    target.serializer->deserialize(broken);
    FAIL() << "a null where a float belongs must not load";
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
