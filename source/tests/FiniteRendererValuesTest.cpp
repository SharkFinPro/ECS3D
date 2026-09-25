#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/components/LightRenderer.h"
#include "objects/components/ModelRenderer.h"
#include "objects/components/Transform.h"
#include "WireTypes.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>

namespace {
  constexpr float notANumber = std::numeric_limits<float>::quiet_NaN();
  constexpr float infinity = std::numeric_limits<float>::infinity();

  template <typename T>
  std::shared_ptr<T> makeComponent(const char* name)
  {
    ComponentRegistry componentRegistry;
    registerDataComponents(componentRegistry);

    return std::dynamic_pointer_cast<T>(componentRegistry.create(name));
  }

  net::MessageReader readerPastTag(const net::Message& message)
  {
    net::MessageReader reader(message);
    static_cast<void>(reader.read<ComponentType>());
    return reader;
  }

  net::Message packedLight(const glm::vec3& color, const float ambient, const glm::vec3& direction)
  {
    net::Message message(net::MessageType::undefined);
    message.write(ComponentType::lightRenderer);
    message.write(true);
    message.write(color);
    message.write(ambient);
    message.write(0.5f);
    message.write(0.25f);
    message.write(direction);
    message.write(30.0f);
    return message;
  }

  net::Message packedModel(const float reflectivity)
  {
    net::Message message(net::MessageType::undefined);
    message.write(ComponentType::modelRenderer);
    message.write(false);
    message.write(false);
    message.write(reflectivity);
    message.write(uuids::uuid());
    message.write(uuids::uuid());
    message.write(uuids::uuid());
    return message;
  }

  net::Message packedTransform(const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale)
  {
    net::Message message(net::MessageType::undefined);
    message.write(ComponentType::transform);
    message.write(position);
    message.write(rotation);
    message.write(scale);
    return message;
  }

  nlohmann::json lightJson(const nlohmann::json& color, const nlohmann::json& ambient)
  {
    return {
      { "color", color },
      { "direction", { 0.0f, 0.0f, 1.0f } },
      { "ambient", ambient },
      { "diffuse", 0.5f },
      { "specular", 0.25f },
      { "coneAngle", 30.0f },
      { "isSpotlight", true }
    };
  }

  nlohmann::json transformJson(const nlohmann::json& position)
  {
    return {
      { "position", position },
      { "rotation", { 4.0f, 5.0f, 6.0f } },
      { "scale", { 7.0f, 8.0f, 9.0f } }
    };
  }
}

TEST(FiniteRendererValues, LightUnpackKeepsPriorValuesForNonFiniteInput)
{
  for (const float bad : { notANumber, infinity })
  {
    const auto light = makeComponent<LightRenderer>("LightRenderer");
    ASSERT_NE(light, nullptr);
    light->setColor({ 0.1f, 0.2f, 0.3f });
    light->setAmbient(0.7f);
    light->setDirection({ 1.0f, 0.0f, 0.0f });

    const auto message = packedLight({ bad, 0.0f, 0.0f }, bad, { 0.0f, bad, 0.0f });
    auto reader = readerPastTag(message);
    light->unpack(reader);

    EXPECT_EQ(light->getColor(), glm::vec3(0.1f, 0.2f, 0.3f));
    EXPECT_FLOAT_EQ(light->getAmbient(), 0.7f);
    EXPECT_EQ(light->getDirection(), glm::vec3(1.0f, 0.0f, 0.0f));

    EXPECT_FLOAT_EQ(light->getDiffuse(), 0.5f);
    EXPECT_FLOAT_EQ(light->getSpecular(), 0.25f);
    EXPECT_FLOAT_EQ(light->getConeAngle(), 30.0f);
    EXPECT_TRUE(light->isSpotLight());
  }
}

TEST(FiniteRendererValues, LightUnpackAppliesFiniteInput)
{
  const auto light = makeComponent<LightRenderer>("LightRenderer");
  ASSERT_NE(light, nullptr);

  const auto message = packedLight({ 0.4f, 0.5f, 0.6f }, 0.9f, { 0.0f, 1.0f, 0.0f });
  auto reader = readerPastTag(message);
  light->unpack(reader);

  EXPECT_EQ(light->getColor(), glm::vec3(0.4f, 0.5f, 0.6f));
  EXPECT_FLOAT_EQ(light->getAmbient(), 0.9f);
  EXPECT_EQ(light->getDirection(), glm::vec3(0.0f, 1.0f, 0.0f));
  EXPECT_FLOAT_EQ(light->getDiffuse(), 0.5f);
}

TEST(FiniteRendererValues, LightLoadFromJSONKeepsPriorValuesForNonFiniteInput)
{
  const auto light = makeComponent<LightRenderer>("LightRenderer");
  ASSERT_NE(light, nullptr);
  light->setColor({ 0.1f, 0.2f, 0.3f });
  light->setAmbient(0.7f);

  light->loadFromJSON(lightJson({ nullptr, 0.0f, 0.0f }, nullptr));
  EXPECT_EQ(light->getColor(), glm::vec3(0.1f, 0.2f, 0.3f));
  EXPECT_FLOAT_EQ(light->getAmbient(), 0.7f);

  light->loadFromJSON(lightJson({ std::numeric_limits<double>::infinity(), 0.0f, 0.0f },
                                std::numeric_limits<double>::quiet_NaN()));
  EXPECT_EQ(light->getColor(), glm::vec3(0.1f, 0.2f, 0.3f));
  EXPECT_FLOAT_EQ(light->getAmbient(), 0.7f);

  EXPECT_FLOAT_EQ(light->getDiffuse(), 0.5f);
  EXPECT_FLOAT_EQ(light->getSpecular(), 0.25f);
  EXPECT_FLOAT_EQ(light->getConeAngle(), 30.0f);
  EXPECT_TRUE(light->isSpotLight());
}

TEST(FiniteRendererValues, LightLoadFromJSONAppliesFiniteInput)
{
  const auto light = makeComponent<LightRenderer>("LightRenderer");
  ASSERT_NE(light, nullptr);

  light->loadFromJSON(lightJson({ 0.4f, 0.5f, 0.6f }, 0.9f));

  EXPECT_EQ(light->getColor(), glm::vec3(0.4f, 0.5f, 0.6f));
  EXPECT_FLOAT_EQ(light->getAmbient(), 0.9f);
  EXPECT_FLOAT_EQ(light->getDiffuse(), 0.5f);
}

TEST(FiniteRendererValues, ModelUnpackKeepsPriorReflectivityForNonFiniteInput)
{
  for (const float bad : { notANumber, infinity })
  {
    const auto model = makeComponent<ModelRenderer>("ModelRenderer");
    ASSERT_NE(model, nullptr);
    model->setReflectivity(0.3f);
    model->setShouldRender(true);

    const auto message = packedModel(bad);
    auto reader = readerPastTag(message);
    model->unpack(reader);

    EXPECT_FLOAT_EQ(model->getReflectivity(), 0.3f);
    EXPECT_FALSE(model->getShouldRender());
  }
}

TEST(FiniteRendererValues, ModelUnpackAppliesFiniteReflectivity)
{
  const auto model = makeComponent<ModelRenderer>("ModelRenderer");
  ASSERT_NE(model, nullptr);
  model->setReflectivity(0.3f);

  const auto message = packedModel(0.8f);
  auto reader = readerPastTag(message);
  model->unpack(reader);

  EXPECT_FLOAT_EQ(model->getReflectivity(), 0.8f);
}

TEST(FiniteRendererValues, ModelLoadFromJSONKeepsPriorReflectivityForNonFiniteInput)
{
  const auto model = makeComponent<ModelRenderer>("ModelRenderer");
  ASSERT_NE(model, nullptr);
  model->setReflectivity(0.3f);

  nlohmann::json data = {
    { "shouldRender", true },
    { "reflectivity", nullptr },
    { "modelUUID", "" },
    { "textureUUID", "" },
    { "specularMapUUID", "" }
  };
  model->loadFromJSON(data);
  EXPECT_FLOAT_EQ(model->getReflectivity(), 0.3f);

  data["reflectivity"] = std::numeric_limits<double>::infinity();
  model->loadFromJSON(data);
  EXPECT_FLOAT_EQ(model->getReflectivity(), 0.3f);
  EXPECT_TRUE(model->getShouldRender());
}

TEST(FiniteRendererValues, ModelLoadFromJSONAppliesFiniteReflectivityAndDefaultsWhenAbsent)
{
  const auto model = makeComponent<ModelRenderer>("ModelRenderer");
  ASSERT_NE(model, nullptr);

  nlohmann::json data = {
    { "shouldRender", true },
    { "reflectivity", 0.6f },
    { "modelUUID", "" },
    { "textureUUID", "" },
    { "specularMapUUID", "" }
  };
  model->loadFromJSON(data);
  EXPECT_FLOAT_EQ(model->getReflectivity(), 0.6f);

  data.erase("reflectivity");
  model->loadFromJSON(data);
  EXPECT_FLOAT_EQ(model->getReflectivity(), 0.0f);
}

TEST(FiniteRendererValues, TransformUnpackKeepsPriorValuesForNonFiniteInput)
{
  for (const float bad : { notANumber, infinity })
  {
    const auto transform = makeComponent<Transform>("Transform");
    ASSERT_NE(transform, nullptr);
    transform->setPosition({ 1.0f, 2.0f, 3.0f });
    transform->setRotation({ 0.0f, 0.0f, 0.0f });
    transform->setScale({ 1.0f, 1.0f, 1.0f });

    const auto message = packedTransform({ bad, 0.0f, 0.0f }, { 4.0f, 5.0f, 6.0f }, { 1.0f, bad, 1.0f });
    auto reader = readerPastTag(message);
    transform->unpack(reader);

    EXPECT_EQ(transform->getLocalPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(transform->getLocalRotation(), glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(transform->getLocalScale(), glm::vec3(1.0f, 1.0f, 1.0f));
  }
}

TEST(FiniteRendererValues, TransformUnpackAppliesFiniteInput)
{
  const auto transform = makeComponent<Transform>("Transform");
  ASSERT_NE(transform, nullptr);

  const auto message = packedTransform({ 1.0f, 2.0f, 3.0f }, { 4.0f, 5.0f, 6.0f }, { 7.0f, 8.0f, 9.0f });
  auto reader = readerPastTag(message);
  transform->unpack(reader);

  EXPECT_EQ(transform->getLocalPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
  EXPECT_EQ(transform->getLocalRotation(), glm::vec3(4.0f, 5.0f, 6.0f));
  EXPECT_EQ(transform->getLocalScale(), glm::vec3(7.0f, 8.0f, 9.0f));
}

TEST(FiniteRendererValues, TransformLoadFromJSONKeepsPriorValuesForNonFiniteInput)
{
  const auto transform = makeComponent<Transform>("Transform");
  ASSERT_NE(transform, nullptr);
  transform->setPosition({ 1.0f, 2.0f, 3.0f });

  transform->loadFromJSON(transformJson({ nullptr, 0.0f, 0.0f }));
  EXPECT_EQ(transform->getLocalPosition(), glm::vec3(1.0f, 2.0f, 3.0f));

  transform->loadFromJSON(transformJson({ std::numeric_limits<double>::infinity(), 0.0f, 0.0f }));
  EXPECT_EQ(transform->getLocalPosition(), glm::vec3(1.0f, 2.0f, 3.0f));

  EXPECT_EQ(transform->getLocalRotation(), glm::vec3(4.0f, 5.0f, 6.0f));
  EXPECT_EQ(transform->getLocalScale(), glm::vec3(7.0f, 8.0f, 9.0f));
}

TEST(FiniteRendererValues, TransformLoadFromJSONAppliesFiniteInput)
{
  const auto transform = makeComponent<Transform>("Transform");
  ASSERT_NE(transform, nullptr);

  transform->loadFromJSON(transformJson({ 1.0f, 2.0f, 3.0f }));

  EXPECT_EQ(transform->getLocalPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
  EXPECT_EQ(transform->getLocalRotation(), glm::vec3(4.0f, 5.0f, 6.0f));
}
