#include "BoxCollider.h"
#include "../../Object.h"
#include "../Transform.h"
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
{}

void BoxCollider::displayGui()
{
  if (displayGuiHeader())
  {
    ImGui::Checkbox("Render Collider", &m_renderCollider);
  }
}

nlohmann::json BoxCollider::serialize()
{
  const nlohmann::json data = {
    { "type", "Collider" },
    { "subType", "Box" },
  };

  return data;
}

void BoxCollider::loadFromJSON(const nlohmann::json& componentData)
{}

void BoxCollider::variableUpdate([[maybe_unused]] const float dt)
{
  if (!m_renderCollider)
  {
    return;
  }

  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      return;
    }
  }

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
    m_renderObject->setPosition(transform->getPosition());
    m_renderObject->setScale(transform->getScale());
    m_renderObject->setOrientationEuler(transform->getRotation());
  }

  renderer->getRenderingManager()->getRenderer3D()->renderObject(
    m_renderObject,
    vke::PipelineType::objectHighlight,
    nullptr
  );
}

glm::vec3 BoxCollider::findFurthestPoint(const glm::vec3& direction)
{
  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      throw std::runtime_error("MeshCollider::findFurthestPoint::Missing transform component");
    }
  }

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
  const auto rotation = transform->getRotation();
  const auto scale = transform->getScale();
  const auto position = transform->getPosition();

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
