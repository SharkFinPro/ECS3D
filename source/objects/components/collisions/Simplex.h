#ifndef SIMPLEX_H
#define SIMPLEX_H

#include <glm/glm.hpp>

struct SupportVertex {
  glm::vec3 vertex;
  glm::vec3 direction;
};

inline bool sameDirection(const glm::vec3& first, const glm::vec3& second)
{
  return glm::dot(first, second) > 0;
}

class Simplex {
public:
  Simplex();

  [[nodiscard]] size_t size() const;

  [[nodiscard]] const glm::vec3& getA() const;
  [[nodiscard]] const glm::vec3& getB() const;
  [[nodiscard]] const glm::vec3& getC() const;
  [[nodiscard]] const glm::vec3& getD() const;

  [[nodiscard]] const SupportVertex& getSupportA() const;
  [[nodiscard]] const SupportVertex& getSupportB() const;
  [[nodiscard]] const SupportVertex& getSupportC() const;
  [[nodiscard]] const SupportVertex& getSupportD() const;

  void removeB();
  void removeC();
  void removeD();

  void addVertex(const SupportVertex& vertex);

private:
  SupportVertex vertices[4]{};
  size_t length;
};



#endif //SIMPLEX_H
