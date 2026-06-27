#include "ModelRenderer.h"
#include <nlohmann/json.hpp>

ModelRenderer::ModelRenderer()
  : Component(ComponentType::modelRenderer)
{}

bool ModelRenderer::getShouldRender() const
{
  return m_shouldRender;
}

void ModelRenderer::setShouldRender(const bool shouldRender)
{
  m_shouldRender = shouldRender;
}

bool ModelRenderer::getUseStandardPipeline() const
{
  return m_useStandardPipeline;
}

void ModelRenderer::setUseStandardPipeline(const bool useStandardPipeline)
{
  m_useStandardPipeline = useStandardPipeline;
}

uuids::uuid ModelRenderer::getModelUUID() const
{
  return m_modelUUID;
}

void ModelRenderer::setModelUUID(const uuids::uuid& modelUUID)
{
  m_modelUUID = modelUUID;
}

uuids::uuid ModelRenderer::getTextureUUID() const
{
  return m_textureUUID;
}

void ModelRenderer::setTextureUUID(const uuids::uuid& textureUUID)
{
  m_textureUUID = textureUUID;
}

uuids::uuid ModelRenderer::getSpecularMapUUID() const
{
  return m_specularMapUUID;
}

void ModelRenderer::setSpecularMapUUID(const uuids::uuid& specularMapUUID)
{
  m_specularMapUUID = specularMapUUID;
}

bool ModelRenderer::canRender() const
{
  return !m_modelUUID.is_nil() && !m_textureUUID.is_nil() && !m_specularMapUUID.is_nil();
}

nlohmann::json ModelRenderer::serialize()
{
  const nlohmann::json data = {
    { "type", "ModelRenderer" },
    { "shouldRender", m_shouldRender },
    { "modelUUID", m_modelUUID.is_nil() ? "" : uuids::to_string(m_modelUUID) },
    { "textureUUID", m_textureUUID.is_nil() ? "" : uuids::to_string(m_textureUUID) },
    { "specularMapUUID", m_specularMapUUID.is_nil() ? "" : uuids::to_string(m_specularMapUUID) }
  };

  return data;
}

void ModelRenderer::loadFromJSON(const nlohmann::json& componentData)
{
  m_shouldRender = componentData.at("shouldRender");

  if (const auto modelUUID = uuids::uuid::from_string(std::string(componentData.at("modelUUID"))); modelUUID.has_value())
  {
    m_modelUUID = modelUUID.value();
  }

  if (const auto textureUUID = uuids::uuid::from_string(std::string(componentData.at("textureUUID"))); textureUUID.has_value())
  {
    m_textureUUID = textureUUID.value();
  }

  if (const auto specularMapUUID = uuids::uuid::from_string(std::string(componentData.at("specularMapUUID"))); specularMapUUID.has_value())
  {
    m_specularMapUUID = specularMapUUID.value();
  }
}
