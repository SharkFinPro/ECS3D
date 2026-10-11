#ifndef CONVEXMESHCOLLIDER_H
#define CONVEXMESHCOLLIDER_H

#include "Collider.h"
#include "ConvexHull.h"
#include <glm/vec3.hpp>
#include <uuid.h>
#include <vector>

// A convex hull collider. The hull lives inline in the component (local-space vertices and triangle
// indices) so the server never needs the model file it was built from; the source model is only a reference
// for display and for rebuilding. The default hull is the unit cube, so a fresh collider behaves like a
// default BoxCollider until a model is picked.
class ConvexMeshCollider final : public Collider {
public:
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

  // The hull's drawable edges (see hullEdges), cached until the hull changes.
  [[nodiscard]] const std::vector<HullEdge>& getEdges();

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  [[nodiscard]] glm::vec3 getPosition() override;
  [[nodiscard]] glm::vec3 getScale() override;
  [[nodiscard]] glm::vec3 getRotation() override;

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  ConvexHull m_hull;
  uuids::uuid m_sourceModel;

  std::vector<glm::vec3> m_worldVertices;

  std::vector<HullEdge> m_edges;
  bool m_edgesDirty = true;

  uint64_t m_currentTransformUpdateID = 255;

  // Editing the collider's own offset or hull doesn't bump the transform's update id, so force a rebuild.
  bool m_meshDirty = true;

  ComponentVariable<glm::vec3> m_position = ComponentVariable(glm::vec3(0));
  ComponentVariable<glm::vec3> m_scale = ComponentVariable(glm::vec3(1));
  ComponentVariable<glm::vec3> m_rotation = ComponentVariable(glm::vec3(0));

  void generateWorldVertices(const std::shared_ptr<Transform>& transform);

  void updateTransformPointer();

  void invalidateGeometry();
};



#endif //CONVEXMESHCOLLIDER_H
