#include <gtest/gtest.h>

#include "TestScene.h"
#include "bindings/BindingContext.h"
#include "bindings/LightRendererBindings.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/LightRenderer.h"

#include <cmath>
#include <memory>
#include <string>
#include <uuid.h>

namespace {
  // BindingContext points at whatever ObjectManager the server's current tick is using; each test wires
  // it to its own scene and must undo that (and drop anything it recorded) so it doesn't leak into a test
  // that runs after it - BindingContext's backing state is static, shared across the whole suite.
  class LightRendererBindingsTest : public testing::Test {
  protected:
    void SetUp() override
    {
      m_scene = fixtures::makeScene();
      m_object = fixtures::addObject(m_scene, "Light");
      m_light = std::make_shared<LightRenderer>();
      m_object->addComponent(m_light);

      BindingContext::setObjectManager(m_scene.objectManager.get());
      m_bindings = LightRendererBindingsProvider::getBindings();
    }

    void TearDown() override
    {
      BindingContext::setObjectManager(nullptr);
      BindingContext::takeComponentEdits();
    }

    fixtures::Scene m_scene;
    std::shared_ptr<Object> m_object;
    std::shared_ptr<LightRenderer> m_light;
    LightRendererBindings m_bindings{};

    [[nodiscard]] std::string uuid() const
    {
      return uuids::to_string(m_object->getUUID());
    }
  };
}

TEST_F(LightRendererBindingsTest, HasIsTrueForAnObjectCarryingALightRenderer)
{
  EXPECT_TRUE(m_bindings.has(uuid().c_str()));
}

TEST_F(LightRendererBindingsTest, GetReturnsTheComponentsValues)
{
  m_light->setSpotLight(true);
  m_light->setColor({ 0.25f, 0.5f, 0.75f });
  m_light->setAmbient(0.1f);
  m_light->setDiffuse(0.2f);
  m_light->setSpecular(0.3f);
  m_light->setDirection({ 1.0f, 0.0f, 0.0f });
  m_light->setConeAngle(45.0f);

  const auto id = uuid();

  EXPECT_TRUE(m_bindings.getIsSpotLight(id.c_str()));

  float r = 0, g = 0, b = 0;
  m_bindings.getColor(id.c_str(), &r, &g, &b);
  EXPECT_NEAR(r, 0.25f, 1e-5f);
  EXPECT_NEAR(g, 0.5f, 1e-5f);
  EXPECT_NEAR(b, 0.75f, 1e-5f);

  EXPECT_NEAR(m_bindings.getAmbient(id.c_str()), 0.1f, 1e-5f);
  EXPECT_NEAR(m_bindings.getDiffuse(id.c_str()), 0.2f, 1e-5f);
  EXPECT_NEAR(m_bindings.getSpecular(id.c_str()), 0.3f, 1e-5f);

  float x = 0, y = 0, z = 0;
  m_bindings.getDirection(id.c_str(), &x, &y, &z);
  EXPECT_NEAR(x, 1.0f, 1e-5f);
  EXPECT_NEAR(y, 0.0f, 1e-5f);
  EXPECT_NEAR(z, 0.0f, 1e-5f);

  EXPECT_NEAR(m_bindings.getConeAngle(id.c_str()), 45.0f, 1e-5f);
}

TEST_F(LightRendererBindingsTest, GetRecordsNoComponentEdit)
{
  const auto id = uuid();

  m_bindings.getIsSpotLight(id.c_str());
  float a = 0, b = 0, c = 0;
  m_bindings.getColor(id.c_str(), &a, &b, &c);
  m_bindings.getAmbient(id.c_str());
  m_bindings.getDiffuse(id.c_str());
  m_bindings.getSpecular(id.c_str());
  m_bindings.getDirection(id.c_str(), &a, &b, &c);
  m_bindings.getConeAngle(id.c_str());

  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
}

TEST_F(LightRendererBindingsTest, SetSpotLightChangesTheComponentAndRecordsOneEdit)
{
  const auto id = uuid();
  ASSERT_FALSE(m_light->isSpotLight());

  m_bindings.setSpotLight(id.c_str(), true);

  EXPECT_TRUE(m_light->isSpotLight());

  // Positive control for the recording behavior every setter below relies on: exactly one edit, for this
  // object, after exactly one set call.
  const auto edits = BindingContext::takeComponentEdits();
  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(edits[0].first, m_object->getUUID());
  EXPECT_EQ(edits[0].second, m_light);
}

TEST_F(LightRendererBindingsTest, SetColorChangesTheComponentAndRecordsOneEdit)
{
  const auto id = uuid();

  m_bindings.setColor(id.c_str(), 0.1f, 0.2f, 0.3f);

  fixtures::expectNear("color", m_light->getColor(), glm::vec3(0.1f, 0.2f, 0.3f));
  EXPECT_EQ(BindingContext::takeComponentEdits().size(), 1u);
}

TEST_F(LightRendererBindingsTest, SetColorIgnoresNonFiniteInput)
{
  const auto id = uuid();
  const auto before = m_light->getColor();

  m_bindings.setColor(id.c_str(), std::nanf(""), 0.2f, 0.3f);

  fixtures::expectNear("color", m_light->getColor(), before);

  // A rejected set is not a mutation - nothing to replicate.
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
}

TEST_F(LightRendererBindingsTest, SetAmbientChangesTheComponentAndRecordsOneEdit)
{
  const auto id = uuid();

  m_bindings.setAmbient(id.c_str(), 0.4f);

  EXPECT_NEAR(m_light->getAmbient(), 0.4f, 1e-5f);
  EXPECT_EQ(BindingContext::takeComponentEdits().size(), 1u);
}

TEST_F(LightRendererBindingsTest, SetAmbientIgnoresNonFiniteInput)
{
  const auto id = uuid();
  const auto before = m_light->getAmbient();

  m_bindings.setAmbient(id.c_str(), std::nanf(""));

  EXPECT_NEAR(m_light->getAmbient(), before, 1e-5f);
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
}

TEST_F(LightRendererBindingsTest, SetDiffuseChangesTheComponentAndRecordsOneEdit)
{
  const auto id = uuid();

  m_bindings.setDiffuse(id.c_str(), 0.5f);

  EXPECT_NEAR(m_light->getDiffuse(), 0.5f, 1e-5f);
  EXPECT_EQ(BindingContext::takeComponentEdits().size(), 1u);
}

TEST_F(LightRendererBindingsTest, SetSpecularChangesTheComponentAndRecordsOneEdit)
{
  const auto id = uuid();

  m_bindings.setSpecular(id.c_str(), 0.6f);

  EXPECT_NEAR(m_light->getSpecular(), 0.6f, 1e-5f);
  EXPECT_EQ(BindingContext::takeComponentEdits().size(), 1u);
}

TEST_F(LightRendererBindingsTest, SetDirectionChangesTheComponentAndRecordsOneEdit)
{
  const auto id = uuid();

  m_bindings.setDirection(id.c_str(), 0.0f, 1.0f, 0.0f);

  fixtures::expectNear("direction", m_light->getDirection(), glm::vec3(0.0f, 1.0f, 0.0f));
  EXPECT_EQ(BindingContext::takeComponentEdits().size(), 1u);
}

TEST_F(LightRendererBindingsTest, SetDirectionAcceptsAZeroVectorWithoutProducingNaN)
{
  const auto id = uuid();

  m_bindings.setDirection(id.c_str(), 0.0f, 0.0f, 0.0f);

  const auto direction = m_light->getDirection();
  EXPECT_TRUE(std::isfinite(direction.x));
  EXPECT_TRUE(std::isfinite(direction.y));
  EXPECT_TRUE(std::isfinite(direction.z));
  fixtures::expectNear("direction", direction, glm::vec3(0.0f, 0.0f, 0.0f));
}

TEST_F(LightRendererBindingsTest, SetDirectionIgnoresNonFiniteInput)
{
  const auto id = uuid();
  const auto before = m_light->getDirection();

  m_bindings.setDirection(id.c_str(), std::nanf(""), 1.0f, 0.0f);

  fixtures::expectNear("direction", m_light->getDirection(), before);
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
}

TEST_F(LightRendererBindingsTest, SetConeAngleClampsAndRecordsOneEdit)
{
  const auto id = uuid();

  m_bindings.setConeAngle(id.c_str(), 200.0f);

  EXPECT_NEAR(m_light->getConeAngle(), LightRenderer::maxConeAngleDegrees, 1e-5f);
  EXPECT_EQ(BindingContext::takeComponentEdits().size(), 1u);
}

TEST_F(LightRendererBindingsTest, SetConeAngleIgnoresNonFiniteInput)
{
  const auto id = uuid();
  const auto before = m_light->getConeAngle();

  m_bindings.setConeAngle(id.c_str(), std::nanf(""));

  EXPECT_NEAR(m_light->getConeAngle(), before, 1e-5f);
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
}

TEST_F(LightRendererBindingsTest, UnknownUuidGetsReturnNeutralDefaults)
{
  const std::string unknown = "12345678-1234-1234-1234-123456789abc";

  EXPECT_FALSE(m_bindings.getIsSpotLight(unknown.c_str()));
  EXPECT_NEAR(m_bindings.getAmbient(unknown.c_str()), 0.0f, 1e-5f);
  EXPECT_NEAR(m_bindings.getDiffuse(unknown.c_str()), 0.0f, 1e-5f);
  EXPECT_NEAR(m_bindings.getSpecular(unknown.c_str()), 0.0f, 1e-5f);
  EXPECT_NEAR(m_bindings.getConeAngle(unknown.c_str()), 0.0f, 1e-5f);
  EXPECT_FALSE(m_bindings.has(unknown.c_str()));
}

TEST_F(LightRendererBindingsTest, UnknownUuidSetsChangeNothingAndRecordNothing)
{
  const std::string unknown = "12345678-1234-1234-1234-123456789abc";

  m_bindings.setSpotLight(unknown.c_str(), true);
  m_bindings.setColor(unknown.c_str(), 0.1f, 0.2f, 0.3f);
  m_bindings.setAmbient(unknown.c_str(), 0.1f);
  m_bindings.setDiffuse(unknown.c_str(), 0.1f);
  m_bindings.setSpecular(unknown.c_str(), 0.1f);
  m_bindings.setDirection(unknown.c_str(), 1.0f, 0.0f, 0.0f);
  m_bindings.setConeAngle(unknown.c_str(), 45.0f);

  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

  // Positive control: the same set of calls against the real object's uuid does record edits, so the
  // empty result above reflects the unknown-uuid guard rather than some other break in the pipeline.
  const auto id = uuid();
  m_bindings.setAmbient(id.c_str(), 0.1f);
  EXPECT_EQ(BindingContext::takeComponentEdits().size(), 1u);
}

TEST_F(LightRendererBindingsTest, MalformedUuidBehavesLikeUnknown)
{
  const char* malformed = "not-a-uuid";

  EXPECT_FALSE(m_bindings.has(malformed));
  EXPECT_FALSE(m_bindings.getIsSpotLight(malformed));

  m_bindings.setAmbient(malformed, 0.5f);
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
}

TEST_F(LightRendererBindingsTest, ObjectWithoutALightRendererBehavesLikeUnknown)
{
  auto plain = fixtures::addObject(m_scene, "NoLight");
  const auto id = uuids::to_string(plain->getUUID());

  EXPECT_FALSE(m_bindings.has(id.c_str()));
  EXPECT_FALSE(m_bindings.getIsSpotLight(id.c_str()));

  m_bindings.setDiffuse(id.c_str(), 0.9f);
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

  // Positive control: has() is true for the fixture's real light, so false above reflects the missing
  // component rather than a broken has() implementation.
  EXPECT_TRUE(m_bindings.has(uuid().c_str()));
}
