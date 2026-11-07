#include <gtest/gtest.h>

#include <RenderComponents.h>

#include <memory>
#include <vector>

namespace {
  std::shared_ptr<Object> makeObject(const std::vector<std::shared_ptr<Component>>& components)
  {
    return std::make_shared<Object>(components, "Test");
  }
}

TEST(RenderComponents, EachFieldPointsAtTheComponentGetComponentReturns)
{
  const auto object = makeObject({
    std::make_shared<Transform>(),
    std::make_shared<ModelRenderer>(),
    std::make_shared<LightRenderer>(),
    std::make_shared<BoxCollider>()
  });

  const auto found = gatherRenderComponents(*object);

  ASSERT_NE(found.transform, nullptr);
  ASSERT_NE(found.modelRenderer, nullptr);
  ASSERT_NE(found.lightRenderer, nullptr);
  ASSERT_NE(found.box, nullptr);
  EXPECT_EQ(found.transform, object->getComponent<Transform>(ComponentType::transform).get());
  EXPECT_EQ(found.modelRenderer, object->getComponent<ModelRenderer>(ComponentType::modelRenderer).get());
  EXPECT_EQ(found.lightRenderer, object->getComponent<LightRenderer>(ComponentType::lightRenderer).get());
  EXPECT_EQ(found.box, object->getComponent<BoxCollider>(ComponentType::collider).get());
}

TEST(RenderComponents, ABoxColliderSetsOnlyTheBoxField)
{
  const auto collider = std::make_shared<BoxCollider>();
  const auto found = gatherRenderComponents(*makeObject({collider}));

  EXPECT_EQ(found.box, collider.get());
  EXPECT_EQ(found.sphere, nullptr);
  EXPECT_EQ(found.capsule, nullptr);
  EXPECT_EQ(found.convexMesh, nullptr);
}

TEST(RenderComponents, ASphereColliderSetsOnlyTheSphereField)
{
  const auto collider = std::make_shared<SphereCollider>();
  const auto found = gatherRenderComponents(*makeObject({collider}));

  EXPECT_EQ(found.sphere, collider.get());
  EXPECT_EQ(found.box, nullptr);
  EXPECT_EQ(found.capsule, nullptr);
  EXPECT_EQ(found.convexMesh, nullptr);
}

TEST(RenderComponents, ACapsuleColliderSetsOnlyTheCapsuleField)
{
  const auto collider = std::make_shared<CapsuleCollider>();
  const auto found = gatherRenderComponents(*makeObject({collider}));

  EXPECT_EQ(found.capsule, collider.get());
  EXPECT_EQ(found.box, nullptr);
  EXPECT_EQ(found.sphere, nullptr);
  EXPECT_EQ(found.convexMesh, nullptr);
}

TEST(RenderComponents, AConvexMeshColliderSetsOnlyTheConvexMeshField)
{
  const auto collider = std::make_shared<ConvexMeshCollider>();
  const auto found = gatherRenderComponents(*makeObject({collider}));

  EXPECT_EQ(found.convexMesh, collider.get());
  EXPECT_EQ(found.box, nullptr);
  EXPECT_EQ(found.sphere, nullptr);
  EXPECT_EQ(found.capsule, nullptr);
}

TEST(RenderComponents, AnObjectWithNoComponentsYieldsAllNull)
{
  const auto empty = gatherRenderComponents(*makeObject({}));

  EXPECT_EQ(empty.transform, nullptr);
  EXPECT_EQ(empty.modelRenderer, nullptr);
  EXPECT_EQ(empty.lightRenderer, nullptr);
  EXPECT_EQ(empty.box, nullptr);
  EXPECT_EQ(empty.sphere, nullptr);
  EXPECT_EQ(empty.capsule, nullptr);
  EXPECT_EQ(empty.convexMesh, nullptr);

  const auto populated = gatherRenderComponents(*makeObject({std::make_shared<Transform>()}));
  EXPECT_NE(populated.transform, nullptr);
}
