#include <gtest/gtest.h>

#include "ComponentRegistry.h"
#include "TestScene.h"
#include "objects/CameraSelection.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Camera.h"
#include "objects/components/PlayerController.h"
#include "objects/components/Transform.h"

#include <cmath>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <optional>
#include <string>
#include <uuid.h>

namespace {
  std::shared_ptr<Camera> addCamera(const fixtures::Scene& scene, const std::shared_ptr<Object>& object,
                                    const bool active)
  {
    auto camera = std::dynamic_pointer_cast<Camera>(scene.componentRegistry->create("Camera"));
    camera->setActive(active);
    object->addComponent(camera);

    return camera;
  }

  void addPlayerController(const fixtures::Scene& scene, const std::shared_ptr<Object>& object, const int slot)
  {
    auto controller = std::dynamic_pointer_cast<PlayerController>(scene.componentRegistry->create("PlayerController"));
    controller->setPlayerSlot(slot);
    object->addComponent(controller);
  }

  bool isFinite(const glm::mat4& matrix)
  {
    for (int column = 0; column < 4; ++column)
    {
      for (int row = 0; row < 4; ++row)
      {
        if (!std::isfinite(matrix[column][row]))
        {
          return false;
        }
      }
    }

    return true;
  }
}

TEST(CameraSelection, NoCameraSelectsNothing)
{
  const auto scene = fixtures::makeScene();
  const auto plain = addObject(scene, "Plain");

  EXPECT_EQ(findActiveCamera(*scene.objectManager, std::nullopt), nullptr);

  addCamera(scene, plain, true);
  EXPECT_EQ(findActiveCamera(*scene.objectManager, std::nullopt), plain);
}

TEST(CameraSelection, InactiveCameraIsSkippedForALaterActiveOne)
{
  const auto scene = fixtures::makeScene();
  const auto inactive = addObject(scene, "Inactive");
  addCamera(scene, inactive, false);

  EXPECT_EQ(findActiveCamera(*scene.objectManager, std::nullopt), nullptr);

  const auto active = addObject(scene, "Active");
  addCamera(scene, active, true);

  EXPECT_EQ(findActiveCamera(*scene.objectManager, std::nullopt), active);
}

TEST(CameraSelection, CameraWithoutATransformIsSkipped)
{
  const auto scene = fixtures::makeScene();
  const auto bare = addObject(scene, "Bare");
  addCamera(scene, bare, true);
  const auto placed = addObject(scene, "Placed");
  addCamera(scene, placed, true);

  ASSERT_EQ(findActiveCamera(*scene.objectManager, std::nullopt), bare);

  bare->removeComponent(fixtures::transformOf(bare));

  EXPECT_EQ(findActiveCamera(*scene.objectManager, std::nullopt), placed);
}

TEST(CameraSelection, RestrictingToAnObjectConsidersOnlyIt)
{
  const auto scene = fixtures::makeScene();
  const auto first = addObject(scene, "First");
  addCamera(scene, first, true);
  const auto second = addObject(scene, "Second");
  const auto secondCamera = addCamera(scene, second, true);

  EXPECT_EQ(findActiveCamera(*scene.objectManager, second->getUUID()), second);
  EXPECT_EQ(findActiveCamera(*scene.objectManager, first->getUUID()), first);

  secondCamera->setActive(false);

  // The first object's active camera must not stand in for the requested one.
  EXPECT_EQ(findActiveCamera(*scene.objectManager, second->getUUID()), nullptr);
  EXPECT_EQ(findActiveCamera(*scene.objectManager, std::nullopt), first);
}

TEST(CameraSelection, FirstActiveCameraInObjectOrderWins)
{
  const auto scene = fixtures::makeScene();
  const auto first = addObject(scene, "First");
  addCamera(scene, first, true);
  const auto second = addObject(scene, "Second");
  addCamera(scene, second, true);

  EXPECT_EQ(findActiveCamera(*scene.objectManager, std::nullopt), first);
}

TEST(CameraSelection, IdentityOrientationLooksAlongTheCameraDirection)
{
  fixtures::expectNear(cameraForward({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }), { 0.0f, 0.0f, -1.0f });
  fixtures::expectNear(cameraForward({ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }), { 1.0f, 0.0f, 0.0f });
}

TEST(CameraSelection, ForwardIsNormalized)
{
  fixtures::expectNear(cameraForward({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -5.0f }), { 0.0f, 0.0f, -1.0f });
}

TEST(CameraSelection, QuarterTurnYawTurnsForward)
{
  fixtures::expectNear(cameraForward({ 0.0f, 90.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }), { -1.0f, 0.0f, 0.0f }, 1e-4f);
}

TEST(CameraSelection, ZeroDirectionFallsBackToMinusZ)
{
  fixtures::expectNear(cameraForward({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }), { 0.0f, 0.0f, -1.0f });
  fixtures::expectNear(cameraForward({ 30.0f, 45.0f, 10.0f }, { 0.0f, 0.0f, 0.0f }), { 0.0f, 0.0f, -1.0f });
}

TEST(CameraSelection, UpIsWorldUpUnlessLookingStraightUpOrDown)
{
  fixtures::expectNear(cameraUp({ 0.0f, 0.0f, -1.0f }), { 0.0f, 1.0f, 0.0f });
  fixtures::expectNear(cameraUp({ 0.0f, 1.0f, 0.0f }), { 0.0f, 0.0f, 1.0f });
  fixtures::expectNear(cameraUp({ 0.0f, -1.0f, 0.0f }), { 0.0f, 0.0f, 1.0f });
}

TEST(CameraSelection, ViewLookingStraightUpIsFinite)
{
  const auto scene = fixtures::makeScene();
  const auto object = addObject(scene, "Skyward", { 1.0f, 2.0f, 3.0f });
  addCamera(scene, object, true)->setDirection({ 0.0f, 1.0f, 0.0f });

  const auto view = cameraViewOf(*object);
  ASSERT_TRUE(view.has_value());
  EXPECT_TRUE(isFinite(view->view));
  fixtures::expectNear(view->position, { 1.0f, 2.0f, 3.0f });

  // A point above the camera lands in front of it (negative view-space z).
  const glm::vec4 above = view->view * glm::vec4(1.0f, 7.0f, 3.0f, 1.0f);
  EXPECT_NEAR(above.z, -5.0f, 1e-4f);
}

TEST(CameraSelection, ViewCarriesTheCameraProjectionFields)
{
  const auto scene = fixtures::makeScene();
  const auto object = addObject(scene, "Cam");
  const auto camera = addCamera(scene, object, true);
  camera->setFov(70.0f);
  camera->setNearPlane(0.5f);
  camera->setFarPlane(250.0f);

  const auto view = cameraViewOf(*object);
  ASSERT_TRUE(view.has_value());
  EXPECT_NEAR(view->fov, 70.0f, 1e-4f);
  EXPECT_NEAR(view->nearPlane, 0.5f, 1e-4f);
  EXPECT_NEAR(view->farPlane, 250.0f, 1e-4f);

  object->removeComponent(fixtures::transformOf(object));
  EXPECT_FALSE(cameraViewOf(*object).has_value());
}

TEST(CameraSelection, LabelIsThePlainNameForAnActiveCamera)
{
  const auto scene = fixtures::makeScene();
  const auto object = addObject(scene, "Cam");
  addCamera(scene, object, true);

  EXPECT_EQ(cameraLabel(*object), "Cam");
}

TEST(CameraSelection, LabelNamesThePlayerSlot)
{
  const auto scene = fixtures::makeScene();
  const auto object = addObject(scene, "Cam");
  addCamera(scene, object, true);
  addPlayerController(scene, object, 2);

  EXPECT_EQ(cameraLabel(*object), "Cam (Player 2)");
}

TEST(CameraSelection, LabelMarksAnInactiveCamera)
{
  const auto scene = fixtures::makeScene();
  const auto object = addObject(scene, "Cam");
  const auto camera = addCamera(scene, object, false);

  EXPECT_EQ(cameraLabel(*object), "Cam - inactive");

  addPlayerController(scene, object, 2);
  EXPECT_EQ(cameraLabel(*object), "Cam (Player 2) - inactive");

  camera->setActive(true);
  EXPECT_EQ(cameraLabel(*object), "Cam (Player 2)");
}

TEST(CameraSelection, PreviewShowsTheChosenCamera)
{
  const auto scene = fixtures::makeScene();
  const auto object = addObject(scene, "Cam");
  addCamera(scene, object, true);

  EXPECT_EQ(cameraPreviewLabel(scene.objectManager.get(), object->getUUID(), "Free-fly"), "Cam");
}

TEST(CameraSelection, PreviewFallsBackToFreeFly)
{
  const auto scene = fixtures::makeScene();
  const auto object = addObject(scene, "Cam");
  addCamera(scene, object, true);
  const auto uuid = object->getUUID();

  ASSERT_EQ(cameraPreviewLabel(scene.objectManager.get(), uuid, "Free-fly"), "Cam");

  EXPECT_EQ(cameraPreviewLabel(scene.objectManager.get(), std::nullopt, "Free-fly"), "Free-fly");
  EXPECT_EQ(cameraPreviewLabel(scene.objectManager.get(), uuids::uuid{}, "Free-fly"), "Free-fly");
  EXPECT_EQ(cameraPreviewLabel(nullptr, uuid, "Free-fly"), "Free-fly");

  scene.objectManager->removeSubtree(object);
  EXPECT_EQ(cameraPreviewLabel(scene.objectManager.get(), uuid, "Free-fly"), "Free-fly");
}
