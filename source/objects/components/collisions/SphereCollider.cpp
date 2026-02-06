#include "SphereCollider.h"
#include "../Transform.h"
#include "../../Object.h"
#include "../../../ECS3D.h"
#include "../../../GuiComponents.h"
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
{
  loadVariable(m_radius);
  loadVariable(m_position);
}

float SphereCollider::getRadius()
{
  updateTransformPointer();

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    const float maxScale = compMax(transform->getScale());
    return m_radius.value() * maxScale;
  }

  return 0;
}

void SphereCollider::displayGui()
{
  if (displayGuiHeader())
  {
    ImGui::Checkbox("Render Collider", &m_renderCollider);
    ImGui::DragFloat("Radius", &m_radius.value(), 0.01f);

    ImGui::PushID("SphereColliderPosition");
    gc::xyzGui("Position", &m_position.value().x, &m_position.value().y, &m_position.value().z);
    ImGui::PopID();
  }
}

nlohmann::json SphereCollider::serialize()
{
  const auto position = m_position.value();

  const nlohmann::json data = {
    { "type", "Collider" },
    { "subType", "Sphere" },
    { "radius", m_radius.value() },
    { "renderCollider", m_renderCollider },
    { "position", { position.x, position.y, position.z } }
  };

  return data;
}

void SphereCollider::loadFromJSON(const nlohmann::json& componentData)
{
  const auto& position = componentData.at("position");
  m_position.set(glm::vec3(position.at(0), position.at(1), position.at(2)));

  m_radius.set(componentData.at("radius"));
  m_renderCollider = componentData.at("renderCollider");
}

void SphereCollider::variableUpdate()
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

    const auto model = assetManager->getAsset<ModelAsset>("assets/models/sphere_3.glb");
    const auto texture = assetManager->getAsset<TextureAsset>("assets/textures/white.png");

    m_renderObject = renderer->getAssetManager()->loadRenderObject(texture->getTexture(), texture->getTexture(), model->getModel());
  }

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    m_renderObject->setPosition(transform->getPosition() + m_position.value());
    m_renderObject->setScale(transform->getScale() * m_radius.value());
  }

  renderer->getRenderingManager()->getRenderer3D()->renderObject(
    m_renderObject,
    vke::PipelineType::objectHighlight,
    nullptr
  );
}

glm::vec3 SphereCollider::getPosition()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  return m_position.value() + transform->getPosition();
}

glm::vec3 SphereCollider::findFurthestPoint(const glm::vec3& direction)
{
  updateTransformPointer();

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    return direction * m_radius.value() * transform->getScale() + transform->getPosition() + m_position.value();
  }

  return { 0, 0, 0 };
}

void SphereCollider::updateTransformPointer()
{
  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      throw std::runtime_error("SphereCollider::updateTransformPointer::Missing transform component");
    }
  }
}
