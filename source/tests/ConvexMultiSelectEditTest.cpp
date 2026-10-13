#include <gtest/gtest.h>

#include "ConvexMeshFixtures.h"
#include "objects/ComponentFieldDelta.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

TEST(ConvexMultiSelectEdit, AHullChangeCarriesVerticesIndicesAndSourceTogether)
{
  ConvexMeshCollider primary;
  const nlohmann::json before = primary.serialize();

  // Same topology as the default cube, so only the vertices differ at the top level.
  ConvexHull doubled = primary.getHull();
  for (auto& vertex : doubled.vertices)
  {
    vertex *= 2.0f;
  }
  ASSERT_TRUE(primary.setHull(doubled));
  primary.setSourceModel(convexFixtures::sourceModel);
  const nlohmann::json after = primary.serialize();

  ConvexMeshCollider target;
  ASSERT_TRUE(target.setHull(convexFixtures::tetrahedron()));

  const auto changed = componentFieldDelta::changedTopLevelKeys(before, after);
  EXPECT_NE(std::ranges::find(changed, std::string("vertices")), changed.end());
  EXPECT_EQ(std::ranges::find(changed, std::string("indices")), changed.end());

  const auto linked = componentFieldDelta::withLinkedKeys(changed, after);
  for (const auto* key : { "vertices", "indices", "sourceModel" })
  {
    EXPECT_NE(std::ranges::find(linked, std::string(key)), linked.end()) << key;
  }

  const auto merged = componentFieldDelta::applyKeyDelta(target.serialize(), before, after, linked);
  EXPECT_NO_THROW(target.loadFromJSON(merged));

  EXPECT_TRUE(isValidConvexHull(target.getHull()));
  EXPECT_EQ(target.getHull().vertices, primary.getHull().vertices);
  EXPECT_EQ(target.getHull().indices, primary.getHull().indices);
  EXPECT_EQ(target.getSourceModel(), convexFixtures::sourceModel);
}

TEST(ConvexMultiSelectEdit, KeysOutsideTheHullGroupAreNotLinked)
{
  ConvexMeshCollider collider;
  const nlohmann::json form = collider.serialize();

  const std::vector<std::string> keys{ "layer" };
  EXPECT_EQ(componentFieldDelta::withLinkedKeys(keys, form), keys);

  // Positive control: a hull key does pull in its group.
  EXPECT_GT(componentFieldDelta::withLinkedKeys({ "indices" }, form).size(), 1u);
}

TEST(ConvexMultiSelectEdit, LinkedKeysKeepACubeTargetValidWhenATetrahedronMoves)
{
  ConvexMeshCollider primary;
  ASSERT_TRUE(primary.setHull(convexFixtures::tetrahedron()));
  const nlohmann::json before = primary.serialize();

  ConvexHull moved = primary.getHull();
  for (auto& vertex : moved.vertices)
  {
    vertex *= 2.0f;
  }
  ASSERT_TRUE(primary.setHull(moved));
  const nlohmann::json after = primary.serialize();

  ConvexMeshCollider target;
  const auto changed = componentFieldDelta::changedTopLevelKeys(before, after);
  ASSERT_EQ(changed, std::vector<std::string>{ "vertices" });

  // Without the link the tetrahedron's four vertices meet the cube's 36 indices.
  ConvexMeshCollider unlinkedTarget;
  EXPECT_THROW(unlinkedTarget.loadFromJSON(
    componentFieldDelta::applyKeyDelta(target.serialize(), before, after, changed)), std::runtime_error);

  const auto linked = componentFieldDelta::withLinkedKeys(changed, after);
  EXPECT_NO_THROW(target.loadFromJSON(componentFieldDelta::applyKeyDelta(target.serialize(), before, after, linked)));
  EXPECT_TRUE(isValidConvexHull(target.getHull()));
  EXPECT_EQ(target.getHull().vertices, primary.getHull().vertices);
}

TEST(ConvexMultiSelectEdit, OnlyMixedHullKeysAreSelectedForAnUnchangedPrimary)
{
  const std::vector<std::string> mixed{ "layer", "indices", "position", "sourceModel" };

  const auto linked = componentFieldDelta::linkedMixedKeys(mixed);
  EXPECT_EQ(linked, (std::vector<std::string>{ "indices", "sourceModel" }));

  EXPECT_TRUE(componentFieldDelta::linkedMixedKeys({ "layer", "position" }).empty());
}
