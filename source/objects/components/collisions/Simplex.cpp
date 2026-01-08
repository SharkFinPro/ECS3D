#include "Simplex.h"

Simplex::Simplex()
  : m_length(0)
{}

size_t Simplex::size() const
{
  return m_length;
}

const glm::vec3& Simplex::getA() const
{
  return m_vertices[0].vertex;
}

const glm::vec3& Simplex::getB() const
{
  return m_vertices[1].vertex;
}

const glm::vec3& Simplex::getC() const
{
  return m_vertices[2].vertex;
}

const glm::vec3& Simplex::getD() const
{
  return m_vertices[3].vertex;
}

const SupportVertex& Simplex::getSupportA() const
{
  return m_vertices[0];
}

const SupportVertex& Simplex::getSupportB() const
{
  return m_vertices[1];
}

const SupportVertex& Simplex::getSupportC() const
{
  return m_vertices[2];
}

const SupportVertex& Simplex::getSupportD() const
{
  return m_vertices[3];
}

void Simplex::removeB()
{
  assert(m_length >= 2);

  m_vertices[1] = m_vertices[2];
  m_vertices[2] = m_vertices[3];

  --m_length;
}

void Simplex::removeC()
{
  assert(m_length >= 3);

  m_vertices[2] = m_vertices[3];

  --m_length;
}

void Simplex::removeD()
{
  assert(m_length >= 4);

  --m_length;
}

void Simplex::addVertex(const SupportVertex& vertex)
{
  for (auto i = m_length; i > 0; --i)
  {
    m_vertices[i] = m_vertices[i - 1];
  }

  m_vertices[0] = vertex;
  ++m_length;
}
