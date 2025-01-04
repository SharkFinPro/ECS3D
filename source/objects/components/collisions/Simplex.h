#ifndef SIMPLEX_H
#define SIMPLEX_H

#include <glm/vec3.hpp>

class Simplex {
public:
  Simplex();

  [[nodiscard]] size_t size() const;

  [[nodiscard]] const glm::vec3& getA() const;
  [[nodiscard]] const glm::vec3& getB() const;
  [[nodiscard]] const glm::vec3& getC() const;
  [[nodiscard]] const glm::vec3& getD() const;

  void removeB();
  void removeC();
  void removeD();

  void addVertex(const glm::vec3& vertex);

private:
  glm::vec3 vertices[4]{};
  size_t length;
};



#endif //SIMPLEX_H
