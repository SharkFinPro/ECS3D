#ifndef BOXCOLLIDER_H
#define BOXCOLLIDER_H

#include "Collider.h"
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <array>
#include <cstdint>
#include <limits>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define ECS3D_BOX_SUPPORT_SSE2
#include <emmintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
#define ECS3D_BOX_SUPPORT_NEON
#include <arm_neon.h>
#endif

constexpr std::array<glm::vec3, 8> boxVertices = {{
  {1.0f, 1.0f, 1.0f},
  {-1.0f, 1.0f, 1.0f},
  {-1.0f, 1.0f, -1.0f},
  {1.0f, 1.0f, -1.0f},
  {1.0f, -1.0f, 1.0f},
  {-1.0f, -1.0f, 1.0f},
  {-1.0f, -1.0f, -1.0f},
  {1.0f, -1.0f, -1.0f}
}};

// A box's world-space corners, also laid out by coordinate so the eight support dots run four at a time.
struct BoxSupport {
  std::array<glm::vec3, boxVertices.size()> vertices{};

  alignas(16) std::array<float, boxVertices.size()> xs{};
  alignas(16) std::array<float, boxVertices.size()> ys{};
  alignas(16) std::array<float, boxVertices.size()> zs{};

  // Fills xs, ys and zs from vertices.
  void splitCoordinates()
  {
    for (size_t i = 0; i < vertices.size(); ++i)
    {
      xs[i] = vertices[i].x;
      ys[i] = vertices[i].y;
      zs[i] = vertices[i].z;
    }
  }

  // A full scan rather than picking the corner from the signs of the axis dots: the two break ties (a
  // direction square to a face) toward different corners, and the response is tuned on the scan's choice.
  // Each dot is glm::dot's own sum, (x * dx + y * dy) + z * dz with every product rounded on its own, so the
  // vector paths give the scalar scan's answer bit for bit.
  [[nodiscard]] glm::vec3 furthestPoint(const glm::vec3& direction) const
  {
    alignas(16) std::array<float, boxVertices.size()> dots{};
    cornerDots(direction, dots);

    float largestDot = std::numeric_limits<float>::lowest();
    glm::vec3 furthestVertex{ 0, 0, 0 };

    for (size_t i = 0; i < dots.size(); ++i)
    {
      if (dots[i] > largestDot)
      {
        largestDot = dots[i];
        furthestVertex = vertices[i];
      }
    }

    return furthestVertex;
  }

private:
  void cornerDots(const glm::vec3& direction, std::array<float, boxVertices.size()>& dots) const
  {
#if defined(ECS3D_BOX_SUPPORT_SSE2)
    const __m128 dx = _mm_set1_ps(direction.x);
    const __m128 dy = _mm_set1_ps(direction.y);
    const __m128 dz = _mm_set1_ps(direction.z);
    for (size_t i = 0; i < dots.size(); i += 4)
    {
      const __m128 px = _mm_mul_ps(_mm_load_ps(&xs[i]), dx);
      const __m128 py = _mm_mul_ps(_mm_load_ps(&ys[i]), dy);
      const __m128 pz = _mm_mul_ps(_mm_load_ps(&zs[i]), dz);
      _mm_store_ps(&dots[i], _mm_add_ps(_mm_add_ps(px, py), pz));
    }
#elif defined(ECS3D_BOX_SUPPORT_NEON)
    const float32x4_t dx = vdupq_n_f32(direction.x);
    const float32x4_t dy = vdupq_n_f32(direction.y);
    const float32x4_t dz = vdupq_n_f32(direction.z);
    for (size_t i = 0; i < dots.size(); i += 4)
    {
      const float32x4_t px = vmulq_f32(vld1q_f32(&xs[i]), dx);
      const float32x4_t py = vmulq_f32(vld1q_f32(&ys[i]), dy);
      const float32x4_t pz = vmulq_f32(vld1q_f32(&zs[i]), dz);
      vst1q_f32(&dots[i], vaddq_f32(vaddq_f32(px, py), pz));
    }
#else
    for (size_t i = 0; i < dots.size(); ++i)
    {
      const float px = xs[i] * direction.x;
      const float py = ys[i] * direction.y;
      const float pz = zs[i] * direction.z;
      dots[i] = px + py + pz;
    }
#endif
  }
};

// The box as the contact manifold reads it: world center, unit local axes and the half extent along each.
struct BoxWorldFrame {
  glm::vec3 center{ 0.0f };
  std::array<glm::vec3, 3> axes{};
  std::array<float, 3> halfExtents{};
};


class BoxCollider final : public Collider {
public:
  BoxCollider();

  // Local (collider-offset) accessors for the editor; getPosition/getScale/getRotation below are the
  // world values the collision system uses - position and rotation offset the transform's, scale
  // multiplies it, matching the matrix the collision mesh is built with.
  [[nodiscard]] glm::vec3 getLocalPosition() const;
  [[nodiscard]] glm::vec3 getLocalScale() const;
  [[nodiscard]] glm::vec3 getLocalRotation() const;

  void setPosition(const glm::vec3& position);
  void setScale(const glm::vec3& scale);
  void setRotation(const glm::vec3& rotation);

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  [[nodiscard]] glm::vec3 getPosition() override;
  [[nodiscard]] glm::vec3 getScale() override;
  [[nodiscard]] glm::vec3 getRotation() override;

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;

  // The geometry findFurthestPoint reads, refreshed first.
  [[nodiscard]] const BoxSupport& getWorldSupport();

  // Built from getPosition, getRotation and getScale, and kept until the transform or the box changes.
  [[nodiscard]] const BoxWorldFrame& getWorldFrame();

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  BoxSupport m_worldSupport;

  BoxWorldFrame m_worldFrame;
  uint64_t m_worldFrameUpdateID = 0;
  uint64_t m_worldFrameRevision = 0;
  bool m_worldFrameValid = false;

  uint64_t m_currentTransformUpdateID = 255;

  // Editing the collider's own offset doesn't bump the transform's update id, so force a mesh rebuild.
  bool m_meshDirty = true;

  ComponentVariable<glm::vec3> m_position = ComponentVariable(glm::vec3(0));
  ComponentVariable<glm::vec3> m_scale = ComponentVariable(glm::vec3(1));
  ComponentVariable<glm::vec3> m_rotation = ComponentVariable(glm::vec3(0));

  void generateTransformedMesh(const Transform& transform);

  void updateTransformPointer();
};



#endif //BOXCOLLIDER_H
