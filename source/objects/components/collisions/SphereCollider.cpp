#include "SphereCollider.h"
#include "../Transform.h"
#include "../../Object.h"
#include "../../../ECS3D.h"
#include "../../../assets/AssetManager.h"
#include "../../../assets/ModelAsset.h"
#include "../../../assets/TextureAsset.h"
#include <glm/gtx/component_wise.inl>
#include <nlohmann/json.hpp>
#include <VulkanEngine/components/assets/AssetManager.h>
#include <VulkanEngine/components/pipelines/implementations/common/PipelineTypes.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/Renderer3D.h>
#include <stdexcept>

SphereCollider::SphereCollider()
  : Collider(ColliderType::sphereCollider, ComponentType::SubComponentType_sphereCollider)
{}

float SphereCollider::getRadius()
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
    const float maxScale = compMax(transform->getScale());
    return m_radius * maxScale;
  }

  return 0;
}

void SphereCollider::displayGui()
{
  if (displayGuiHeader())
  {
    ImGui::Checkbox("Render Collider", &m_renderCollider);
    ImGui::DragFloat("Radius", &m_radius);
  }
}

nlohmann::json SphereCollider::serialize()
{
  const nlohmann::json data = {
    { "type", "Collider" },
    { "subType", "Sphere" },
    { "radius", m_radius },
  };

  return data;
}

void SphereCollider::loadFromJSON(const nlohmann::json& componentData)
{
  m_radius = componentData.at("radius");
}

void SphereCollider::variableUpdate([[maybe_unused]] const float dt)
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

    const auto model = assetManager->getAsset<ModelAsset>("assets/models/sphere_3.glb");
    const auto texture = assetManager->getAsset<TextureAsset>("assets/textures/white.png");

    m_renderObject = renderer->getAssetManager()->loadRenderObject(texture->getTexture(), texture->getTexture(), model->getModel());
  }

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    m_renderObject->setPosition(transform->getPosition());
    m_renderObject->setScale(transform->getScale() * m_radius);
    m_renderObject->setOrientationEuler(transform->getRotation());
  }

  renderer->getRenderingManager()->getRenderer3D()->renderObject(
    m_renderObject,
    vke::PipelineType::objectHighlight,
    nullptr
  );
}

glm::vec3 SphereCollider::findFurthestPoint(const glm::vec3& direction)
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
    return direction * m_radius * transform->getScale() + transform->getPosition();
  }

  return { 0, 0, 0 };
}
