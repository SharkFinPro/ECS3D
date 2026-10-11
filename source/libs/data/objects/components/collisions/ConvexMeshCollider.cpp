#include "ConvexMeshCollider.h"
#include "../FiniteCheck.h"
#include "../RotationConvention.h"
#include "../Transform.h"
#include "../../Object.h"
#include "WireTypes.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <limits>
#include <stdexcept>
#include <string>
#include <Protocol.h>

namespace {
  [[nodiscard]] std::vector<glm::vec3> readVertices(const nlohmann::json& flat)
  {
    if (!flat.is_array() || flat.size() % 3 != 0 || flat.size() > maxConvexHullVertices * 3)
    {
      throw std::runtime_error("ConvexMesh: malformed vertices");
    }

    std::vector<glm::vec3> vertices;
    vertices.reserve(flat.size() / 3);

    for (size_t i = 0; i < flat.size(); i += 3)
    {
      vertices.emplace_back(finiteCheck::readFloatOrNaN(flat.at(i)), finiteCheck::readFloatOrNaN(flat.at(i + 1)),
                            finiteCheck::readFloatOrNaN(flat.at(i + 2)));
    }

    return vertices;
  }

  [[nodiscard]] std::vector<uint32_t> readIndices(const nlohmann::json& flat)
  {
    if (!flat.is_array() || flat.size() > maxConvexHullIndices)
    {
      throw std::runtime_error("ConvexMesh: malformed indices");
    }

    std::vector<uint32_t> indices;
    indices.reserve(flat.size());

    for (const auto& index : flat)
    {
      indices.push_back(index.get<uint32_t>());
    }

    return indices;
  }

  // A count and then that many values, refused before anything is reserved when the count is over limit.
  template <typename T>
  [[nodiscard]] std::vector<T> readCounted(net::MessageReader& reader, const size_t limit, const char* what)
  {
    const auto count = reader.read<uint32_t>();

    if (count > limit)
    {
      throw RefusedPayload(std::string("ConvexMesh: ") + what + " count over the limit");
    }

    std::vector<T> values;
    values.reserve(count);

    for (uint32_t i = 0; i < count; ++i)
    {
      values.push_back(reader.read<T>());
    }

    return values;
  }

  [[nodiscard]] uuids::uuid readSourceModel(const nlohmann::json& componentData)
  {
    const auto it = componentData.find("sourceModel");

    if (it == componentData.end() || !it->is_string())
    {
      return {};
    }

    return uuids::uuid::from_string(it->get<std::string>()).value_or(uuids::uuid{});
  }
}

ConvexMeshCollider::ConvexMeshCollider()
  : Collider(ColliderType::convexMeshCollider, ComponentType::SubComponentType_convexMeshCollider),
    m_hull(makeUnitCubeHull())
{
  loadVariable(m_position);
  loadVariable(m_scale);
  loadVariable(m_rotation);
}

glm::vec3 ConvexMeshCollider::getLocalPosition() const
{
  return m_position.get();
}

glm::vec3 ConvexMeshCollider::getLocalScale() const
{
  return m_scale.get();
}

glm::vec3 ConvexMeshCollider::getLocalRotation() const
{
  return m_rotation.get();
}

void ConvexMeshCollider::setPosition(const glm::vec3& position)
{
  if (!finiteCheck::isFinite(position))
  {
    return;
  }

  m_position.set(position);
  invalidateGeometry();
}

void ConvexMeshCollider::setScale(const glm::vec3& scale)
{
  if (!finiteCheck::isFinite(scale))
  {
    return;
  }

  m_scale.set(scale);
  invalidateGeometry();
}

void ConvexMeshCollider::setRotation(const glm::vec3& rotation)
{
  if (!finiteCheck::isFinite(rotation))
  {
    return;
  }

  m_rotation.set(rotation);
  invalidateGeometry();
}

const uuids::uuid& ConvexMeshCollider::getSourceModel() const
{
  return m_sourceModel;
}

void ConvexMeshCollider::setSourceModel(const uuids::uuid& sourceModel)
{
  m_sourceModel = sourceModel;
}

const ConvexHull& ConvexMeshCollider::getHull() const
{
  return m_hull;
}

bool ConvexMeshCollider::setHull(ConvexHull hull)
{
  if (!isValidConvexHull(hull))
  {
    return false;
  }

  m_hull = std::move(hull);
  invalidateGeometry();

  return true;
}

const std::vector<glm::vec3>& ConvexMeshCollider::getWorldVertices()
{
  updateTransformPointer();

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    if (m_meshDirty || m_currentTransformUpdateID != transform->getWorldUpdateID())
    {
      generateWorldVertices(transform);
      m_meshDirty = false;
    }
  }

  return m_worldVertices;
}

const std::vector<HullEdge>& ConvexMeshCollider::getEdges()
{
  if (m_edgesDirty)
  {
    m_edges = hullEdges(m_hull);
    m_edgesDirty = false;
  }

  return m_edges;
}

nlohmann::json ConvexMeshCollider::serialize()
{
  const auto position = m_position.getInitialValue();
  const auto rotation = m_rotation.getInitialValue();
  const auto scale = m_scale.getInitialValue();

  nlohmann::json vertices = nlohmann::json::array();
  for (const auto& vertex : m_hull.vertices)
  {
    vertices.push_back(vertex.x);
    vertices.push_back(vertex.y);
    vertices.push_back(vertex.z);
  }

  const nlohmann::json data = {
    { "type", "Collider" },
    { "subType", "ConvexMesh" },
    { "position", { position.x, position.y, position.z } },
    { "rotation", { rotation.x, rotation.y, rotation.z } },
    { "scale", { scale.x, scale.y, scale.z } },
    { "sourceModel", m_sourceModel.is_nil() ? "" : uuids::to_string(m_sourceModel) },
    { "vertices", vertices },
    { "indices", m_hull.indices },
    { "renderCollider", m_renderCollider },
    { "isTrigger", m_isTrigger },
    { "layer", m_layer },
    { "mask", m_mask }
  };

  return data;
}

void ConvexMeshCollider::loadFromJSON(const nlohmann::json& componentData)
{
  // Everything is read and checked before anything is assigned, so a malformed field leaves the collider as
  // it was.
  const auto& position = componentData.at("position");
  const auto& rotation = componentData.at("rotation");
  const auto& scale = componentData.at("scale");

  const glm::vec3 positionValue(position.at(0).get<float>(), position.at(1).get<float>(), position.at(2).get<float>());
  const glm::vec3 rotationValue(rotation.at(0).get<float>(), rotation.at(1).get<float>(), rotation.at(2).get<float>());
  const glm::vec3 scaleValue(scale.at(0).get<float>(), scale.at(1).get<float>(), scale.at(2).get<float>());

  ConvexHull hull{ readVertices(componentData.at("vertices")), readIndices(componentData.at("indices")) };

  if (!isValidConvexHull(hull))
  {
    throw std::runtime_error("ConvexMesh: invalid hull");
  }

  // value() (not at()): the same optional flags every collider reads this way.
  const bool renderCollider = componentData.value("renderCollider", false);
  const bool isTrigger = componentData.value("isTrigger", false);
  const uint32_t layer = componentData.value("layer", 0u);
  const uint32_t mask = componentData.value("mask", 0xFFFFFFFFu);
  const uuids::uuid sourceModel = readSourceModel(componentData);

  m_hull = std::move(hull);
  m_sourceModel = sourceModel;
  m_position.set(positionValue);
  m_rotation.set(rotationValue);
  m_scale.set(scaleValue);
  m_renderCollider = renderCollider;
  m_isTrigger = isTrigger;
  setLayer(layer);
  m_mask = mask;

  invalidateGeometry();
}

glm::vec3 ConvexMeshCollider::getPosition()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  return m_position.get() + transform->getPosition();
}

glm::vec3 ConvexMeshCollider::getScale()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  return m_scale.get() * transform->getScale();
}

glm::vec3 ConvexMeshCollider::getRotation()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  if (!transform->hasParentTransform())
  {
    return m_rotation.get() + transform->getRotation();
  }

  return quatToEulerDegrees(transform->getOrientation() * eulerDegreesToQuat(m_rotation.get()));
}

glm::vec3 ConvexMeshCollider::findFurthestPoint(const glm::vec3& direction)
{
  const auto& vertices = getWorldVertices();

  float largestDot = std::numeric_limits<float>::lowest();
  glm::vec3 furthestVertex{ 0, 0, 0 };

  for (const auto& vertex : vertices)
  {
    if (const float currentDot = dot(vertex, direction); currentDot > largestDot)
    {
      largestDot = currentDot;
      furthestVertex = vertex;
    }
  }

  return furthestVertex;
}

void ConvexMeshCollider::pack(net::Message& message) const
{
  message.write(ComponentType::SubComponentType_convexMeshCollider);

  message.write(m_renderCollider);
  message.write(m_position.get());
  message.write(m_scale.get());
  message.write(m_rotation.get());
  message.write(m_sourceModel);

  message.write(static_cast<uint32_t>(m_hull.vertices.size()));
  for (const auto& vertex : m_hull.vertices)
  {
    message.write(vertex);
  }

  message.write(static_cast<uint32_t>(m_hull.indices.size()));
  for (const uint32_t index : m_hull.indices)
  {
    message.write(index);
  }

  message.write(m_isTrigger);
  message.write(m_layer);
  message.write(m_mask);
}

void ConvexMeshCollider::unpack(net::MessageReader& messageReader)
{
  const bool renderCollider = messageReader.read<bool>();
  const auto position = messageReader.read<glm::vec3>();
  const auto scale = messageReader.read<glm::vec3>();
  const auto rotation = messageReader.read<glm::vec3>();
  const auto sourceModel = messageReader.read<uuids::uuid>();

  // Each count is checked before anything is reserved or read, so a hostile count cannot allocate.
  ConvexHull hull;
  hull.vertices = readCounted<glm::vec3>(messageReader, maxConvexHullVertices, "vertex");
  hull.indices = readCounted<uint32_t>(messageReader, maxConvexHullIndices, "index");

  const bool isTrigger = messageReader.read<bool>();
  const auto layer = messageReader.read<uint32_t>();
  const auto mask = messageReader.read<uint32_t>();

  if (!isValidConvexHull(hull))
  {
    throw RefusedPayload("ConvexMesh: invalid hull");
  }

  m_renderCollider = renderCollider;
  m_position.set(position);
  m_scale.set(scale);
  m_rotation.set(rotation);
  m_sourceModel = sourceModel;
  m_hull = std::move(hull);
  m_isTrigger = isTrigger;
  setLayer(layer);
  m_mask = mask;

  invalidateGeometry();
}

void ConvexMeshCollider::generateWorldVertices(const std::shared_ptr<Transform>& transform)
{
  const auto scale = transform->getScale() * m_scale.get();
  const auto position = transform->getPosition() + m_position.get();

  glm::mat4 rotationMatrix;
  if (!transform->hasParentTransform())
  {
    // The same additive Euler sum getRotation reports, which the narrow phase and queries rebuild.
    const auto rotation = transform->getRotation() + m_rotation.get();
    rotationMatrix = rotate(glm::mat4(1.0f), glm::radians(rotation.z), {0, 0, 1})
      * rotate(glm::mat4(1.0f), glm::radians(rotation.y), {0, 1, 0})
      * rotate(glm::mat4(1.0f), glm::radians(rotation.x), {1, 0, 0});
  }
  else
  {
    rotationMatrix = glm::mat4_cast(transform->getOrientation() * eulerDegreesToQuat(m_rotation.get()));
  }

  const auto transformationMatrix = translate(glm::mat4(1.0f), position)
    * rotationMatrix
    * glm::scale(glm::mat4(1.0f), scale);

  m_worldVertices.resize(m_hull.vertices.size());
  for (size_t i = 0; i < m_hull.vertices.size(); ++i)
  {
    m_worldVertices[i] = glm::vec3(transformationMatrix * glm::vec4(m_hull.vertices[i], 1.0f));
  }

  m_currentTransformUpdateID = transform->getWorldUpdateID();
}

void ConvexMeshCollider::updateTransformPointer()
{
  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      throw std::runtime_error("ConvexMeshCollider::updateTransformPointer::Missing transform component");
    }
  }
}

void ConvexMeshCollider::invalidateGeometry()
{
  m_meshDirty = true;
  m_edgesDirty = true;
  invalidateBoundingBox();
}
