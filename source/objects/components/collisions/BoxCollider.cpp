#include "BoxCollider.h"
#include "../../Object.h"
#include "../Transform.h"
#include "../../../GuiComponents.h"
#include "../../../assets/AssetManager.h"
#include "../../../assets/ModelAsset.h"
#include "../../../assets/TextureAsset.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <VulkanEngine/components/assets/AssetManager.h>
#include <VulkanEngine/components/pipelines/implementations/common/PipelineTypes.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/Renderer3D.h>
#include <limits>
#include <stdexcept>

BoxCollider::BoxCollider()
  : Collider(ColliderType::boxCollider, ComponentType::SubComponentType_boxCollider)
{
  loadVariable(m_position);
  loadVariable(m_scale);
  loadVariable(m_rotation);
}

void BoxCollider::displayGui()
{
  if (displayGuiHeader())
  {
    ImGui::Checkbox("Render Collider", &m_renderCollider);

    ImGui::PushID("BoxColliderPosition");
    gc::xyzGui("Position", &m_position.value().x, &m_position.value().y, &m_position.value().z);
    ImGui::PopID();

    ImGui::PushID("BoxColliderRotation");
    gc::xyzGui("Rotation", &m_rotation.value().x, &m_rotation.value().y, &m_rotation.value().z);
    ImGui::PopID();

    ImGui::PushID("BoxColliderScale");
    gc::xyzGui("Scale", &m_scale.value().x, &m_scale.value().y, &m_scale.value().z);

    float combinedScale = (m_scale.value().x + m_scale.value().y + m_scale.value().z) / 3.0f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Scale All");
    ImGui::SameLine(110.0f);
    if (ImGui::DragFloat("##Scale All", &combinedScale, 0.1f))
    {
      m_scale.value().x = m_scale.value().y = m_scale.value().z = combinedScale;
    }

    ImGui::PopID();
  }
}

nlohmann::json BoxCollider::serialize()
{
  const auto position = m_position.value();
  const auto rotation = m_rotation.value();
  const auto scale = m_scale.value();

  const nlohmann::json data = {
    { "type", "Collider" },
    { "subType", "Box" },
    { "position", { position.x, position.y, position.z } },
    { "rotation", { rotation.x, rotation.y, rotation.z } },
    { "scale", { scale.x, scale.y, scale.z } }
  };

  return data;
}

void BoxCollider::loadFromJSON(const nlohmann::json& componentData)
{
  const auto& position = componentData.at("position");
  const auto& rotation = componentData.at("rotation");
  const auto& scale = componentData.at("scale");

  m_position.set(glm::vec3(position.at(0), position.at(1), position.at(2)));
  m_rotation.set(glm::vec3(rotation.at(0), rotation.at(1), rotation.at(2)));
  m_scale.set(glm::vec3(scale.at(0), scale.at(1), scale.at(2)));
}

void BoxCollider::variableUpdate([[maybe_unused]] const float dt)
{
  if (!m_renderCollider)
  {
    return;
  }

  updateTransformPointer();

  const auto renderer = m_owner->getManager()->getECS()->getRenderer();

  if (!m_renderObject)
  {
    const auto assetManager = m_owner->getManager()->getECS()->getAssetManager();

    const auto model = assetManager->getAsset<ModelAsset>("assets/models/cube_1x1x1.glb");
    const auto texture = assetManager->getAsset<TextureAsset>("assets/textures/white.png");

    m_renderObject = renderer->getAssetManager()->loadRenderObject(texture->getTexture(), texture->getTexture(), model->getModel());
  }

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    m_renderObject->setPosition(transform->getPosition() + m_position.value());
    m_renderObject->setScale(transform->getScale() * m_scale.value());
    m_renderObject->setOrientationEuler(transform->getRotation() + m_rotation.value());
  }

  renderer->getRenderingManager()->getRenderer3D()->renderObject(
    m_renderObject,
    vke::PipelineType::objectHighlight,
    nullptr
  );
}

glm::vec3 BoxCollider::getPosition()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  return m_position.value() + transform->getPosition();
}

glm::vec3 BoxCollider::getScale()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  return m_scale.value() + transform->getScale();
}

glm::vec3 BoxCollider::getRotation()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  return m_rotation.value() + transform->getRotation();
}

glm::vec3 BoxCollider::findFurthestPoint(const glm::vec3& direction)
{
  updateTransformPointer();

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    if (m_currentTransformUpdateID != transform->getUpdateID())
    {
      generateTransformedMesh(transform);
    }
  }

  float largestDot = std::numeric_limits<float>::lowest();
  glm::vec3 furthestVertex{ 0, 0, 0 };

  for (auto& vertex : m_transformedBoxVertices)
  {
    if (const float currentDot = dot(vertex, direction); currentDot > largestDot)
    {
      largestDot = currentDot;
      furthestVertex = vertex;
    }
  }

  return furthestVertex;
}

void BoxCollider::generateTransformedMesh(const std::shared_ptr<Transform>& transform)
{
  const auto rotation = transform->getRotation() + m_rotation.value();
  const auto scale = transform->getScale() * m_scale.value();
  const auto position = transform->getPosition() + m_position.value();

  const auto transformationMatrix = translate(glm::mat4(1.0f), position)
    * rotate(glm::mat4(1.0f), glm::radians(rotation.z), {0, 0, 1})
    * rotate(glm::mat4(1.0f), glm::radians(rotation.y), {0, 1, 0})
    * rotate(glm::mat4(1.0f), glm::radians(rotation.x), {1, 0, 0})
    * glm::scale(glm::mat4(1.0f), scale);

  for (size_t i = 0; i < boxVertices.size(); ++i)
  {
    const auto transformedVertex = transformationMatrix * glm::vec4(boxVertices[i], 1.0f);

    m_transformedBoxVertices[i] = glm::vec3(transformedVertex);
  }

  m_currentTransformUpdateID = transform->getUpdateID();
}

void BoxCollider::updateTransformPointer()
{
  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      throw std::runtime_error("BoxCollider::updateTransformPointer::Missing transform component");
    }
  }
}
