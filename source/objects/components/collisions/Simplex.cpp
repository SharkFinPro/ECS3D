#include "Simplex.h"

Simplex::Simplex()
  : length(0)
{}

size_t Simplex::size() const
{
  return length;
}

const glm::vec3 & Simplex::getA() const
{
  return vertices[0];
}

const glm::vec3 & Simplex::getB() const
{
  return vertices[1];
}

const glm::vec3 & Simplex::getC() const
{
  return vertices[2];
}

const glm::vec3 & Simplex::getD() const
{
  return vertices[3];
}

void Simplex::removeB()
{
  if (length < 1)
  {
    return;
  }

  vertices[1] = vertices[2];
  vertices[2] = vertices[3];

  length--;
}

void Simplex::removeC()
{
  if (length < 2)
  {
    return;
  }

  vertices[2] = vertices[3];

  length--;
}

void Simplex::removeD()
{
  if (length < 3)
  {
    return;
  }

  length--;
}

void Simplex::addVertex(const glm::vec3 vertex)
{
  for (auto i = length; i > 0; i--)
  {
    vertices[i] = vertices[i - 1];
  }

  vertices[0] = vertex;
  length++;
}
