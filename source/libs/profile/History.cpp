#include "History.h"

#include <stdexcept>

namespace profile {
  History::History(const std::size_t capacity)
  {
    if (capacity == 0)
    {
      throw std::invalid_argument("History capacity must be at least 1");
    }

    m_samples.resize(capacity);
  }

  void History::push(const Sample& sample)
  {
    m_samples[m_next] = sample;
    m_next = (m_next + 1) % m_samples.size();

    if (m_count < m_samples.size())
    {
      ++m_count;
    }
  }

  std::size_t History::size() const
  {
    return m_count;
  }

  std::size_t History::capacity() const
  {
    return m_samples.size();
  }

  const Sample& History::at(const std::size_t indexFromOldest) const
  {
    if (indexFromOldest >= m_count)
    {
      throw std::out_of_range("History index out of range");
    }

    const auto oldest = (m_next + m_samples.size() - m_count) % m_samples.size();
    return m_samples[(oldest + indexFromOldest) % m_samples.size()];
  }

  const Sample& History::newest() const
  {
    if (m_count == 0)
    {
      throw std::out_of_range("History is empty");
    }

    return at(m_count - 1);
  }

  const Sample* History::findSequence(const uint64_t sequence) const
  {
    for (std::size_t i = 0; i < m_count; ++i)
    {
      const auto& sample = at(i);

      if (sample.sequence == sequence)
      {
        return &sample;
      }
    }

    return nullptr;
  }

  DurationRing::DurationRing(const std::size_t capacity)
  {
    if (capacity == 0)
    {
      throw std::invalid_argument("DurationRing capacity must be at least 1");
    }

    m_values.resize(capacity);
  }

  void DurationRing::push(const uint32_t micros)
  {
    m_values[m_next] = micros;
    m_next = (m_next + 1) % m_values.size();

    if (m_count < m_values.size())
    {
      ++m_count;
    }
  }

  std::size_t DurationRing::size() const
  {
    return m_count;
  }

  uint32_t DurationRing::at(const std::size_t indexFromOldest) const
  {
    if (indexFromOldest >= m_count)
    {
      throw std::out_of_range("DurationRing index out of range");
    }

    const auto oldest = (m_next + m_values.size() - m_count) % m_values.size();
    return m_values[(oldest + indexFromOldest) % m_values.size()];
  }

  std::vector<uint32_t> DurationRing::values() const
  {
    std::vector<uint32_t> result;
    result.reserve(m_count);

    for (std::size_t i = 0; i < m_count; ++i)
    {
      result.push_back(at(i));
    }

    return result;
  }
}
