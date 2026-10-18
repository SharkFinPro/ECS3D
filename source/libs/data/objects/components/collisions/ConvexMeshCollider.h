#ifndef CONVEXMESHCOLLIDER_H
#define CONVEXMESHCOLLIDER_H

#include "Collider.h"
#include "ConvexHull.h"
#include "ConvexHullValidation.h"
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <uuid.h>
#include <cstdint>
#include <optional>
#include <vector>

// A convex hull collider. The hull lives inline in the component (local-space vertices and triangle
// indices) so the server never needs the model file it was built from; the source model is only a reference
// for display and for rebuilding. The default hull is the unit cube, so a fresh collider behaves like a
// default BoxCollider until a model is picked.
class ConvexMeshCollider final : public Collider {
public:
  // The hull's world-space mass distribution, filled by the sim. Derived state: never serialized or packed.
  struct MassCache {
    uint64_t generation = 0;
    float volume = 0.0f;
    glm::vec3 centroid{ 0.0f };
    glm::mat3 covariance{ 0.0f };
  };

  ConvexMeshCollider();

  // Local (collider-offset) accessors for the editor; getPosition/getScale/getRotation below are the
  // world values, composed the way BoxCollider's are.
  [[nodiscard]] glm::vec3 getLocalPosition() const;
  [[nodiscard]] glm::vec3 getLocalScale() const;
  [[nodiscard]] glm::vec3 getLocalRotation() const;

  void setPosition(const glm::vec3& position);
  void setScale(const glm::vec3& scale);
  void setRotation(const glm::vec3& rotation);

  [[nodiscard]] const uuids::uuid& getSourceModel() const;
  void setSourceModel(const uuids::uuid& sourceModel);

  [[nodiscard]] const ConvexHull& getHull() const;

  // Refuses (returns false, hull unchanged) a hull that is not isValidConvexHull.
  [[nodiscard]] bool setHull(ConvexHull hull);

  // The hull's vertices under the collider's current world placement, refreshed when the transform moved.
  [[nodiscard]] const std::vector<glm::vec3>& getWorldVertices();

  // Counts the times the world vertices were rebuilt; a MassCache is current only while its generation matches.
  [[nodiscard]] uint64_t getWorldVerticesGeneration() const;

  [[nodiscard]] const std::optional<MassCache>& getMassCache() const;

  void setMassCache(const MassCache& cache);

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  [[nodiscard]] glm::vec3 getPosition() override;
  [[nodiscard]] glm::vec3 getScale() override;
  [[nodiscard]] glm::vec3 getRotation() override;

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

  // The live and authored offsets differ, so switching between them changes the cached world vertices.
  void start() override;

  void stop() override;

private:
  ConvexHull m_hull;
  uuids::uuid m_sourceModel;

  std::vector<glm::vec3> m_worldVertices;

  uint64_t m_currentTransformUpdateID = 255;

  uint64_t m_worldVerticesGeneration = 0;

  std::optional<MassCache> m_massCache;

  // Editing the collider's own offset or hull doesn't bump the transform's update id, so force a rebuild.
  bool m_meshDirty = true;

  ComponentVariable<glm::vec3> m_position = ComponentVariable(glm::vec3(0));
  ComponentVariable<glm::vec3> m_scale = ComponentVariable(glm::vec3(1));
  ComponentVariable<glm::vec3> m_rotation = ComponentVariable(glm::vec3(0));

  // Every persisted field, read and checked as a whole before any of it is assigned.
  struct Data {
    glm::vec3 position{ 0.0f };
    glm::vec3 scale{ 1.0f };
    glm::vec3 rotation{ 0.0f };
    uuids::uuid sourceModel;
    ConvexHull hull;
    bool renderCollider = false;
    bool isTrigger = false;
    uint32_t layer = 0;
    uint32_t mask = 0xFFFFFFFFu;
  };

  void generateWorldVertices(const std::shared_ptr<Transform>& transform);

  [[nodiscard]] std::shared_ptr<Transform> ownerTransform();

  void assign(Data data);

  void invalidateGeometry();
};

#endif //CONVEXMESHCOLLIDER_H
