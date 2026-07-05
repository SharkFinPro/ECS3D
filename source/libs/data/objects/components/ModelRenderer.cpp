#include "ModelRenderer.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>

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

float ModelRenderer::getReflectivity() const
{
  return m_reflectivity;
}

void ModelRenderer::setReflectivity(const float reflectivity)
{
  m_reflectivity = reflectivity;
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
    { "useStandardPipeline", m_useStandardPipeline },
    { "reflectivity", m_reflectivity },
    { "modelUUID", m_modelUUID.is_nil() ? "" : uuids::to_string(m_modelUUID) },
    { "textureUUID", m_textureUUID.is_nil() ? "" : uuids::to_string(m_textureUUID) },
    { "specularMapUUID", m_specularMapUUID.is_nil() ? "" : uuids::to_string(m_specularMapUUID) }
  };

  return data;
}

void ModelRenderer::loadFromJSON(const nlohmann::json& componentData)
{
  m_shouldRender = componentData.at("shouldRender");

  // value() with defaults: these fields are absent from projects saved before they existed.
  m_useStandardPipeline = componentData.value("useStandardPipeline", true);
  m_reflectivity = componentData.value("reflectivity", 0.0f);

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

void ModelRenderer::pack(net::Message& message) const
{
  message.write(ComponentType::modelRenderer);

  message.write(m_shouldRender);
  message.write(m_useStandardPipeline);
  message.write(m_reflectivity);

  message.write(m_modelUUID);
  message.write(m_textureUUID);
  message.write(m_specularMapUUID);
}

void ModelRenderer::unpack(net::MessageReader& messageReader)
{
  m_shouldRender = messageReader.read<bool>();
  m_useStandardPipeline = messageReader.read<bool>();
  m_reflectivity = messageReader.read<float>();

  m_modelUUID = messageReader.read<uuids::uuid>();
  m_textureUUID = messageReader.read<uuids::uuid>();
  m_specularMapUUID = messageReader.read<uuids::uuid>();
}
