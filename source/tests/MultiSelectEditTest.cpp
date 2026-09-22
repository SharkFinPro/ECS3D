#include <gtest/gtest.h>

#include "TestScene.h"
#include "objects/ComponentFieldDelta.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Script.h"
#include "objects/components/Transform.h"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// Headless coverage for the Inspector's multi-selection editing logic (componentFieldDelta.h): which
// components are common to a selection, which of a common component's fields disagree across it, and how
// a changed field is applied to one object's own component without touching its other fields. None of
// this depends on ImGui, so it is exercised directly rather than through the editor.

// Common-component computation: a positive control (RigidBody, on every object) alongside a negative one
// (a collider only some objects carry), and box vs. sphere colliders kept apart by subtype.
TEST(MultiSelectEdit, CommonComponentsRequireEveryObjectToShareTheSignature)
{
  fixtures::Scene scene;
  const auto a = fixtures::addObject(scene, "A");
  const auto b = fixtures::addObject(scene, "B");
  const auto c = fixtures::addObject(scene, "C");

  fixtures::addRigidBody(a);
  fixtures::addRigidBody(b);
  fixtures::addRigidBody(c);

  // Only a and b get a collider, and of different subtypes - neither should count as common.
  fixtures::addBoxCollider(a);
  fixtures::addSphereCollider(b, 1.0f);

  const auto common = componentFieldDelta::commonComponentSignatures({ a, b, c });

  // Positive: every object has a Transform (added by the Object constructor) and a RigidBody.
  EXPECT_NE(std::ranges::find(common, std::string("Transform")), common.end());
  EXPECT_NE(std::ranges::find(common, std::string("Rigid Body")), common.end());

  // Negative: the collider is not on every object, and even a/b's colliders don't match each other's
  // subtype, so neither collider signature is common.
  EXPECT_EQ(std::ranges::find(common, std::string("Box Collider")), common.end());
  EXPECT_EQ(std::ranges::find(common, std::string("Sphere Collider")), common.end());

  // allComponentSignatures is the superset the "N components not shared" count is derived from: it still
  // names both collider subtypes even though neither is common.
  const auto all = componentFieldDelta::allComponentSignatures({ a, b, c });
  EXPECT_NE(std::ranges::find(all, std::string("Box Collider")), all.end());
  EXPECT_NE(std::ranges::find(all, std::string("Sphere Collider")), all.end());
}

TEST(MultiSelectEdit, FindComponentBySignatureLocatesEachObjectsOwnComponent)
{
  fixtures::Scene scene;
  const auto a = fixtures::addObject(scene, "A");
  const auto b = fixtures::addObject(scene, "B");

  const auto rigidA = fixtures::addRigidBody(a);
  const auto rigidB = fixtures::addRigidBody(b);

  const auto foundA = componentFieldDelta::findComponentBySignature(a, "Rigid Body");
  const auto foundB = componentFieldDelta::findComponentBySignature(b, "Rigid Body");

  ASSERT_TRUE(foundA);
  ASSERT_TRUE(foundB);
  // Each object's own component instance, not the other's and not a copy.
  EXPECT_EQ(foundA, rigidA);
  EXPECT_EQ(foundB, rigidB);
  EXPECT_NE(foundA, foundB);

  EXPECT_EQ(componentFieldDelta::findComponentBySignature(a, "Sphere Collider"), nullptr);
}

// Mixed-field detection: a field that differs across the selection is flagged, a field that happens to
// match everywhere is not - the negative control that proves this isn't just "always mixed".
TEST(MultiSelectEdit, MixedTopLevelKeysFlagsOnlyFieldsThatDisagree)
{
  fixtures::Scene scene;
  const auto a = fixtures::addObject(scene, "A");
  const auto b = fixtures::addObject(scene, "B");

  const auto rigidA = fixtures::addRigidBody(a);
  const auto rigidB = fixtures::addRigidBody(b);

  rigidA->setMass(5.0f);
  rigidB->setMass(9.0f);

  rigidA->setFriction(0.4f);
  rigidB->setFriction(0.4f);

  const auto mixed = componentFieldDelta::mixedTopLevelKeys({ rigidA->serialize(), rigidB->serialize() });

  EXPECT_NE(std::ranges::find(mixed, std::string("mass")), mixed.end());
  EXPECT_EQ(std::ranges::find(mixed, std::string("friction")), mixed.end());
}

TEST(MultiSelectEdit, MixedTopLevelKeysIsEmptyForASingleObject)
{
  fixtures::Scene scene;
  const auto a = fixtures::addObject(scene, "A");
  const auto rigidA = fixtures::addRigidBody(a);

  EXPECT_TRUE(componentFieldDelta::mixedTopLevelKeys({ rigidA->serialize() }).empty());
}

// The key property applying a multi-select edit depends on: only the changed field is carried onto
// another object's component, and that object's own differing fields survive untouched (never overwritten
// by the field-source object's values).
TEST(MultiSelectEdit, ApplyingAChangedFieldLeavesTheTargetsOtherFieldsAlone)
{
  fixtures::Scene scene;
  const auto a = fixtures::addObject(scene, "A");
  const auto b = fixtures::addObject(scene, "B");

  const auto rigidA = fixtures::addRigidBody(a);
  const auto rigidB = fixtures::addRigidBody(b);

  rigidA->setMass(5.0f);
  rigidA->setFriction(0.4f);
  const auto before = rigidA->serialize();

  rigidB->setMass(1.0f);
  rigidB->setFriction(0.9f);
  const auto bBefore = rigidB->serialize();

  // Simulate the widget editing a's mass this frame.
  rigidA->setMass(7.0f);
  const auto after = rigidA->serialize();

  const auto changedKeys = componentFieldDelta::changedTopLevelKeys(before, after);
  ASSERT_EQ(changedKeys.size(), 1u);
  EXPECT_EQ(changedKeys.front(), "mass");

  const auto merged = componentFieldDelta::applyKeyDelta(bBefore, before, after, changedKeys);

  EXPECT_FLOAT_EQ(merged.at("mass").get<float>(), 7.0f);
  // b's own friction, which a never touched and which differs from a's, must survive the merge.
  EXPECT_FLOAT_EQ(merged.at("friction").get<float>(), 0.9f);

  rigidB->loadFromJSON(merged);
  EXPECT_FLOAT_EQ(rigidB->getMass(), 7.0f);
  EXPECT_FLOAT_EQ(rigidB->getFriction(), 0.9f);
}

// Nested values (a vec3 stored as an array) are replaced whole by key, not merged component-wise.
TEST(MultiSelectEdit, ApplyingAChangedVec3KeyReplacesTheWholeArray)
{
  fixtures::Scene scene;
  const auto a = fixtures::addObject(scene, "A");
  const auto b = fixtures::addObject(scene, "B");

  const auto transformA = fixtures::transformOf(a);
  const auto transformB = fixtures::transformOf(b);

  transformA->setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
  transformB->setPosition(glm::vec3(1.0f, 2.0f, 3.0f));
  transformA->setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
  transformB->setRotation(glm::vec3(9.0f, 9.0f, 9.0f));

  const auto before = transformA->serialize();
  const auto bBefore = transformB->serialize();

  transformA->setPosition(glm::vec3(4.0f, 5.0f, 6.0f));
  const auto after = transformA->serialize();

  const auto changedKeys = componentFieldDelta::changedTopLevelKeys(before, after);
  ASSERT_EQ(changedKeys.size(), 1u);
  EXPECT_EQ(changedKeys.front(), "position");

  const auto merged = componentFieldDelta::applyKeyDelta(bBefore, before, after, changedKeys);

  ASSERT_TRUE(merged.at("position").is_array());
  EXPECT_FLOAT_EQ(merged.at("position").at(0).get<float>(), 4.0f);
  EXPECT_FLOAT_EQ(merged.at("position").at(1).get<float>(), 5.0f);
  EXPECT_FLOAT_EQ(merged.at("position").at(2).get<float>(), 6.0f);

  // b's own rotation, untouched by the delta, must survive whole.
  ASSERT_TRUE(merged.at("rotation").is_array());
  EXPECT_FLOAT_EQ(merged.at("rotation").at(0).get<float>(), 9.0f);
  EXPECT_FLOAT_EQ(merged.at("rotation").at(1).get<float>(), 9.0f);
  EXPECT_FLOAT_EQ(merged.at("rotation").at(2).get<float>(), 9.0f);
}

namespace {
  // A "Mover" script instance with the two exposed float fields ScriptEditor.cpp's widgets read/write:
  // {name, type, value}. Shared by the two Script tests below.
  std::shared_ptr<Script> addMoverScript(const std::shared_ptr<Object>& object, const float speed,
                                         const float power)
  {
    auto script = std::make_shared<Script>("Mover");
    script->setFields(nlohmann::json::array({
      { { "name", "speed" }, { "type", "float" }, { "value", speed } },
      { { "name", "power" }, { "type", "float" }, { "value", power } }
    }));
    object->addComponent(script);

    return script;
  }
}

// Regression for the clobber bug: Script components serialize every exposed field under one top-level
// "fields" array, so a naive whole-key replace would overwrite every other object's unrelated fields
// whenever any one field changed. This must fail before the per-entry merge fix.
TEST(MultiSelectEdit, ApplyingAChangedScriptFieldLeavesOtherFieldsAlone)
{
  fixtures::Scene scene;
  const auto a = fixtures::addObject(scene, "A");
  const auto b = fixtures::addObject(scene, "B");

  const auto scriptA = addMoverScript(a, 1.0f, 5.0f);
  const auto scriptB = addMoverScript(b, 1.0f, 9.0f);

  const auto before = scriptA->serialize();

  // Simulate the widget editing only a's "speed" field this frame.
  auto fields = scriptA->getFields();
  fields[0]["value"] = 2.0f;
  scriptA->setFields(fields);
  const auto after = scriptA->serialize();

  const auto changedKeys = componentFieldDelta::changedTopLevelKeys(before, after);
  ASSERT_EQ(changedKeys.size(), 1u);
  EXPECT_EQ(changedKeys.front(), "fields");

  const auto merged = componentFieldDelta::applyKeyDelta(scriptB->serialize(), before, after, changedKeys);
  scriptB->loadFromJSON(merged);

  const auto resultFields = scriptB->getFields();
  ASSERT_EQ(resultFields.size(), 2u);
  EXPECT_FLOAT_EQ(resultFields[0].at("value").get<float>(), 2.0f); // speed propagated from a
  EXPECT_FLOAT_EQ(resultFields[1].at("value").get<float>(), 9.0f); // b's own power, untouched
}

// Mixed detection at field-entry granularity: a field that differs is named ("fields.speed"), a field
// that matches everywhere ("power") is the negative control.
TEST(MultiSelectEdit, MixedTopLevelKeysReportsScriptFieldsByName)
{
  fixtures::Scene scene;
  const auto a = fixtures::addObject(scene, "A");
  const auto b = fixtures::addObject(scene, "B");

  const auto scriptA = addMoverScript(a, 1.0f, 5.0f);
  const auto scriptB = addMoverScript(b, 9.0f, 5.0f);

  const auto mixed = componentFieldDelta::mixedTopLevelKeys({ scriptA->serialize(), scriptB->serialize() });

  EXPECT_NE(std::ranges::find(mixed, std::string("fields.speed")), mixed.end());
  EXPECT_EQ(std::ranges::find(mixed, std::string("fields.power")), mixed.end());
  // The whole "fields" key itself is not reported - the per-entry names are more precise.
  EXPECT_EQ(std::ranges::find(mixed, std::string("fields")), mixed.end());
}
