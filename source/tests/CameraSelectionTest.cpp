#include <gtest/gtest.h>

#include "ComponentRegistry.h"
#include "TestScene.h"
#include "objects/CameraSelection.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Camera.h"
#include "objects/components/PlayerController.h"
#include "objects/components/Transform.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
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

TEST(CameraSelection, UpThresholdSitsJustBelowStraightUp)
{
  const auto tilted = [](const float y) { return glm::vec3(std::sqrt(1.0f - y * y), y, 0.0f); };

  fixtures::expectNear(cameraUp(tilted(0.99995f)), { 0.0f, 0.0f, 1.0f });
  fixtures::expectNear(cameraUp(tilted(-0.99995f)), { 0.0f, 0.0f, 1.0f });
  fixtures::expectNear(cameraUp(tilted(0.9998f)), { 0.0f, 1.0f, 0.0f });
  fixtures::expectNear(cameraUp(tilted(-0.9998f)), { 0.0f, 1.0f, 0.0f });
}

TEST(CameraSelection, PitchTurnsForwardAboutX)
{
  fixtures::expectNear(cameraForward({ 90.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }), { 0.0f, 1.0f, 0.0f }, 1e-4f);
}

TEST(CameraSelection, EulerAnglesApplyPitchBeforeYaw)
{
  // Pitch 45 lifts (0,0,-1) to (0,s,-s); yaw 90 then swings -z onto -x. The other order would give (-1,0,0).
  const float s = std::sqrt(0.5f);
  fixtures::expectNear(cameraForward({ 45.0f, 90.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }), { -s, s, 0.0f }, 1e-4f);
}

TEST(CameraSelection, ViewMapsPointsAheadOfTheCameraToNegativeZ)
{
  const auto scene = fixtures::makeScene();
  const auto object = addObject(scene, "Cam");
  addCamera(scene, object, true);

  const auto view = cameraViewOf(*object);
  ASSERT_TRUE(view.has_value());

  const glm::vec4 ahead = view->view * glm::vec4(0.0f, 0.0f, -5.0f, 1.0f);
  const glm::vec4 behind = view->view * glm::vec4(0.0f, 0.0f, 5.0f, 1.0f);
  EXPECT_NEAR(ahead.x, 0.0f, 1e-4f);
  EXPECT_NEAR(ahead.y, 0.0f, 1e-4f);
  EXPECT_NEAR(ahead.z, -5.0f, 1e-4f);
  EXPECT_NEAR(behind.z, 5.0f, 1e-4f);
}

namespace {
  constexpr float pi = 3.14159265358979f;
  constexpr float cameraFovDegrees = 60.0f;
  constexpr float viewWidth = 800.0f;
  constexpr float viewHeight = 400.0f;

  CameraView viewOfCameraAt(const fixtures::Scene& scene, const glm::vec3& position, const glm::vec3& rotation)
  {
    const auto object = addObject(scene, "Cam", position);
    addCamera(scene, object, true)->setFov(cameraFovDegrees);
    fixtures::transformOf(object)->setRotation(rotation);

    return cameraViewOf(*object).value();
  }

  float angleBetween(const glm::vec3& a, const glm::vec3& b)
  {
    return std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f));
  }
}

TEST(CameraRayThrough, ViewCenterLooksAlongTheCameraForward)
{
  const auto scene = fixtures::makeScene();
  const auto camera = viewOfCameraAt(scene, { 1.0f, 2.0f, 3.0f }, { 0.0f, 0.0f, 0.0f });

  const auto ray = cameraRayThrough(camera, viewWidth / 2.0f, viewHeight / 2.0f, viewWidth, viewHeight);
  ASSERT_TRUE(ray.has_value());
  fixtures::expectNear(ray->origin, { 1.0f, 2.0f, 3.0f });
  fixtures::expectNear(ray->direction, { 0.0f, 0.0f, -1.0f }, 1e-4f);
  EXPECT_NEAR(glm::length(ray->direction), 1.0f, 1e-5f);
}

TEST(CameraRayThrough, RightEdgeTiltsByTheHorizontalHalfFieldOfView)
{
  const auto scene = fixtures::makeScene();
  const auto camera = viewOfCameraAt(scene, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f });

  const auto ray = cameraRayThrough(camera, viewWidth, viewHeight / 2.0f, viewWidth, viewHeight);
  ASSERT_TRUE(ray.has_value());

  const float aspect = viewWidth / viewHeight;
  const float expected = std::atan(std::tan(cameraFovDegrees * pi / 360.0f) * aspect);
  EXPECT_NEAR(angleBetween(ray->direction, { 0.0f, 0.0f, -1.0f }), expected, 1e-4f);
  EXPECT_GT(ray->direction.x, 0.0f);
  EXPECT_NEAR(ray->direction.y, 0.0f, 1e-5f);

  // The left edge mirrors it, so the sign above comes from the cursor and not from a constant skew.
  const auto left = cameraRayThrough(camera, 0.0f, viewHeight / 2.0f, viewWidth, viewHeight);
  ASSERT_TRUE(left.has_value());
  EXPECT_LT(left->direction.x, 0.0f);
}

TEST(CameraRayThrough, TopEdgeTiltsUpByHalfTheVerticalFieldOfView)
{
  const auto scene = fixtures::makeScene();
  const auto camera = viewOfCameraAt(scene, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f });

  const auto top = cameraRayThrough(camera, viewWidth / 2.0f, 0.0f, viewWidth, viewHeight);
  ASSERT_TRUE(top.has_value());
  EXPECT_NEAR(angleBetween(top->direction, { 0.0f, 0.0f, -1.0f }), cameraFovDegrees * pi / 360.0f, 1e-4f);
  EXPECT_GT(top->direction.y, 0.0f);

  const auto bottom = cameraRayThrough(camera, viewWidth / 2.0f, viewHeight, viewWidth, viewHeight);
  ASSERT_TRUE(bottom.has_value());
  EXPECT_LT(bottom->direction.y, 0.0f);
}

TEST(CameraRayThrough, ARotatedCameraTurnsTheRayWithIt)
{
  const auto scene = fixtures::makeScene();
  const auto camera = viewOfCameraAt(scene, { 5.0f, 0.0f, 0.0f }, { 0.0f, 90.0f, 0.0f });

  const auto center = cameraRayThrough(camera, viewWidth / 2.0f, viewHeight / 2.0f, viewWidth, viewHeight);
  ASSERT_TRUE(center.has_value());
  fixtures::expectNear(center->origin, { 5.0f, 0.0f, 0.0f });
  fixtures::expectNear(center->direction, { -1.0f, 0.0f, 0.0f }, 1e-4f);

  // Facing -x, the screen's right is -z.
  const auto right = cameraRayThrough(camera, viewWidth, viewHeight / 2.0f, viewWidth, viewHeight);
  ASSERT_TRUE(right.has_value());
  EXPECT_LT(right->direction.z, 0.0f);
  EXPECT_LT(right->direction.x, 0.0f);
}

TEST(CameraRayThrough, ADegenerateViewOrACursorOutsideItHasNoRay)
{
  const auto scene = fixtures::makeScene();
  const auto camera = viewOfCameraAt(scene, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f });

  // Positive control for every case below: the same cursor in a usable view.
  EXPECT_TRUE(cameraRayThrough(camera, 10.0f, 10.0f, viewWidth, viewHeight).has_value());
  EXPECT_TRUE(cameraRayThrough(camera, 0.0f, 0.0f, viewWidth, viewHeight).has_value());
  EXPECT_TRUE(cameraRayThrough(camera, viewWidth, viewHeight, viewWidth, viewHeight).has_value());

  EXPECT_FALSE(cameraRayThrough(camera, 0.0f, 0.0f, 0.0f, viewHeight).has_value());
  EXPECT_FALSE(cameraRayThrough(camera, 0.0f, 0.0f, viewWidth, 0.5f).has_value());
  EXPECT_FALSE(cameraRayThrough(camera, 0.0f, 0.0f, -viewWidth, viewHeight).has_value());

  EXPECT_FALSE(cameraRayThrough(camera, -1.0f, 10.0f, viewWidth, viewHeight).has_value());
  EXPECT_FALSE(cameraRayThrough(camera, 10.0f, -1.0f, viewWidth, viewHeight).has_value());
  EXPECT_FALSE(cameraRayThrough(camera, viewWidth + 1.0f, 10.0f, viewWidth, viewHeight).has_value());
  EXPECT_FALSE(cameraRayThrough(camera, 10.0f, viewHeight + 1.0f, viewWidth, viewHeight).has_value());
  EXPECT_FALSE(cameraRayThrough(camera, std::nanf(""), 10.0f, viewWidth, viewHeight).has_value());
}
