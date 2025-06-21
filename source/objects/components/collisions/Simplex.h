#ifndef SIMPLEX_H
#define SIMPLEX_H

#include <glm/vec3.hpp>

struct SupportVertex {
  glm::vec3 vertex;
  glm::vec3 direction;
};

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
