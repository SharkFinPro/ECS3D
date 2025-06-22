#include "Simplex.h"

Simplex::Simplex()
  : length(0)
{}

size_t Simplex::size() const
{
  return length;
}

const glm::vec3& Simplex::getA() const
{
  return vertices[0].vertex;
}

const glm::vec3& Simplex::getB() const
{
  return vertices[1].vertex;
}

const glm::vec3& Simplex::getC() const
{
  return vertices[2].vertex;
}

const glm::vec3& Simplex::getD() const
{
  return vertices[3].vertex;
}

const SupportVertex& Simplex::getSupportA() const
{
  return vertices[0];
}

const SupportVertex& Simplex::getSupportB() const
{
  return vertices[1];
}

const SupportVertex& Simplex::getSupportC() const
{
  return vertices[2];
}

const SupportVertex& Simplex::getSupportD() const
{
  return vertices[3];
}

void Simplex::removeB()
{
  assert(length >= 2);

  vertices[1] = vertices[2];
  vertices[2] = vertices[3];

  --length;
}

void Simplex::removeC()
{
  assert(length >= 3);

  vertices[2] = vertices[3];

  --length;
}

void Simplex::removeD()
{
  assert(length >= 4);

  --length;
}

void Simplex::addVertex(const SupportVertex& vertex)
{
  for (auto i = length; i > 0; --i)
  {
    vertices[i] = vertices[i - 1];
  }

  vertices[0] = vertex;
  ++length;
}
