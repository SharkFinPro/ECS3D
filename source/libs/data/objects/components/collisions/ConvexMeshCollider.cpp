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
  [[nodiscard]] glm::vec3 readVec3(const nlohmann::json& value)
  {
    return { value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>() };
  }

  // A non-number reads as NaN, which isValidConvexHull then refuses.
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
      vertices.emplace_back(finiteCheck::readFloatOrNaN(flat[i]), finiteCheck::readFloatOrNaN(flat[i + 1]),
                            finiteCheck::readFloatOrNaN(flat[i + 2]));
    }

    return vertices;
  }

  [[nodiscard]] std::vector<uint32_t> readIndices(const nlohmann::json& flat)
  {
    if (!flat.is_array() || flat.size() > maxConvexHullIndices)
    {
      throw std::runtime_error("ConvexMesh: malformed indices");
    }

    return flat.get<std::vector<uint32_t>>();
  }

  // A count and then that many values; the count is checked before anything is reserved.
  template <typename T>
  [[nodiscard]] std::vector<T> readCounted(net::MessageReader& reader, const size_t limit)
  {
    const auto count = reader.read<uint32_t>();

    if (count > limit)
    {
      throw RefusedPayload("ConvexMesh: count over the limit");
    }

    std::vector<T> values;
    values.reserve(count);

    for (uint32_t i = 0; i < count; ++i)
    {
      values.push_back(reader.read<T>());
    }

    return values;
  }

  template <typename T>
  void writeCounted(net::Message& message, const std::vector<T>& values)
  {
    message.write(static_cast<uint32_t>(values.size()));

    for (const T& value : values)
    {
      message.write(value);
    }
  }

  void requireValidHull(const ConvexHull& hull)
  {
    if (!isValidConvexHull(hull))
    {
      throw RefusedPayload("ConvexMesh: invalid hull");
    }
  }

  // A non-finite offset would save as json null, as the setters' refusal avoids.
  template <typename D>
  void requireValidData(const D& data)
  {
    if (!finiteCheck::isFinite(data.position) || !finiteCheck::isFinite(data.rotation)
      || !finiteCheck::isFinite(data.scale))
    {
      throw RefusedPayload("ConvexMesh: non-finite offset");
    }

    requireValidHull(data.hull);
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
  if (finiteCheck::isFinite(position))
  {
    m_position.set(position);
    invalidateGeometry();
  }
}

void ConvexMeshCollider::setScale(const glm::vec3& scale)
{
  if (finiteCheck::isFinite(scale))
  {
    m_scale.set(scale);
    invalidateGeometry();
  }
}

void ConvexMeshCollider::setRotation(const glm::vec3& rotation)
{
  if (finiteCheck::isFinite(rotation))
  {
    m_rotation.set(rotation);
    invalidateGeometry();
  }
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
  const std::shared_ptr<Transform> transform = ownerTransform();

  if (m_meshDirty || m_currentTransformUpdateID != transform->getWorldUpdateID())
  {
    generateWorldVertices(transform);
    m_meshDirty = false;
  }

  return m_worldVertices;
}

uint64_t ConvexMeshCollider::getWorldVerticesGeneration() const
{
  return m_worldVerticesGeneration;
}

const std::optional<ConvexMeshCollider::MassCache>& ConvexMeshCollider::getMassCache() const
{
  return m_massCache;
}

void ConvexMeshCollider::setMassCache(const MassCache& cache)
{
  m_massCache = cache;
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

  return {
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
}

void ConvexMeshCollider::loadFromJSON(const nlohmann::json& componentData)
{
  Data data;
  data.position = readVec3(componentData.at("position"));
  data.rotation = readVec3(componentData.at("rotation"));
  data.scale = readVec3(componentData.at("scale"));
  data.hull = { readVertices(componentData.at("vertices")), readIndices(componentData.at("indices")) };
  requireValidData(data);

  // value() (not at()): the same optional flags every collider reads this way.
  data.renderCollider = componentData.value("renderCollider", false);
  data.isTrigger = componentData.value("isTrigger", false);
  data.layer = componentData.value("layer", 0u);
  data.mask = componentData.value("mask", 0xFFFFFFFFu);

  if (const auto it = componentData.find("sourceModel"); it != componentData.end() && it->is_string())
  {
    data.sourceModel = uuids::uuid::from_string(it->get<std::string>()).value_or(uuids::uuid{});
  }

  assign(std::move(data));
}

glm::vec3 ConvexMeshCollider::getPosition()
{
  return m_position.get() + ownerTransform()->getPosition();
}

glm::vec3 ConvexMeshCollider::getScale()
{
  return m_scale.get() * ownerTransform()->getScale();
}

glm::vec3 ConvexMeshCollider::getRotation()
{
  const std::shared_ptr<Transform> transform = ownerTransform();

  if (!transform->hasParentTransform())
  {
    return m_rotation.get() + transform->getRotation();
  }

  return quatToEulerDegrees(transform->getOrientation() * eulerDegreesToQuat(m_rotation.get()));
}

glm::vec3 ConvexMeshCollider::findFurthestPoint(const glm::vec3& direction)
{
  float largestDot = std::numeric_limits<float>::lowest();
  glm::vec3 furthestVertex{ 0, 0, 0 };

  for (const auto& vertex : getWorldVertices())
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
  writeCounted(message, m_hull.vertices);
  writeCounted(message, m_hull.indices);
  message.write(m_isTrigger);
  message.write(m_layer);
  message.write(m_mask);
}

void ConvexMeshCollider::unpack(net::MessageReader& messageReader)
{
  Data data;
  data.renderCollider = messageReader.read<bool>();
  data.position = messageReader.read<glm::vec3>();
  data.scale = messageReader.read<glm::vec3>();
  data.rotation = messageReader.read<glm::vec3>();
  data.sourceModel = messageReader.read<uuids::uuid>();
  data.hull.vertices = readCounted<glm::vec3>(messageReader, maxConvexHullVertices);
  data.hull.indices = readCounted<uint32_t>(messageReader, maxConvexHullIndices);
  data.isTrigger = messageReader.read<bool>();
  data.layer = messageReader.read<uint32_t>();
  data.mask = messageReader.read<uint32_t>();
  requireValidData(data);

  assign(std::move(data));
}

void ConvexMeshCollider::start()
{
  Collider::start();

  invalidateGeometry();
}

void ConvexMeshCollider::stop()
{
  Collider::stop();

  invalidateGeometry();
}

void ConvexMeshCollider::generateWorldVertices(const std::shared_ptr<Transform>& transform)
{
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

  const auto transformationMatrix = translate(glm::mat4(1.0f), transform->getPosition() + m_position.get())
    * rotationMatrix
    * glm::scale(glm::mat4(1.0f), transform->getScale() * m_scale.get());

  m_worldVertices.resize(m_hull.vertices.size());
  for (size_t i = 0; i < m_hull.vertices.size(); ++i)
  {
    m_worldVertices[i] = glm::vec3(transformationMatrix * glm::vec4(m_hull.vertices[i], 1.0f));
  }

  m_currentTransformUpdateID = transform->getWorldUpdateID();
  ++m_worldVerticesGeneration;
}

std::shared_ptr<Transform> ConvexMeshCollider::ownerTransform()
{
  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);
  }

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  if (!transform)
  {
    throw std::runtime_error("ConvexMeshCollider::ownerTransform::Missing transform component");
  }

  return transform;
}

void ConvexMeshCollider::assign(Data data)
{
  m_hull = std::move(data.hull);
  m_sourceModel = data.sourceModel;
  m_position.set(data.position);
  m_rotation.set(data.rotation);
  m_scale.set(data.scale);
  m_renderCollider = data.renderCollider;
  m_isTrigger = data.isTrigger;
  setLayer(data.layer);
  m_mask = data.mask;

  invalidateGeometry();
}

void ConvexMeshCollider::invalidateGeometry()
{
  m_meshDirty = true;
  m_massCache.reset();
  invalidateBoundingBox();
}
