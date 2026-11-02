#include "ProfileStats.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace profile {
  namespace {
    template <typename Extract>
    std::vector<uint32_t> windowValues(const History& history, const std::size_t window, Extract extract)
    {
      const auto n = std::min(window, history.size());
      std::vector<uint32_t> values;
      values.reserve(n);

      for (std::size_t i = history.size() - n; i < history.size(); ++i)
      {
        values.push_back(extract(history.at(i)));
      }

      return values;
    }

    Summary summarize(const std::vector<uint32_t>& values)
    {
      Summary summary;

      if (values.empty())
      {
        return summary;
      }

      summary.samples = values.size();
      summary.last = values.back();
      summary.max = *std::max_element(values.begin(), values.end());
      summary.average = static_cast<double>(std::accumulate(values.begin(), values.end(), uint64_t{0})) /
                        static_cast<double>(values.size());
      summary.p95 = percentile(values, 95.0);
      return summary;
    }
  }

  uint32_t percentile(std::vector<uint32_t> values, const double p)
  {
    if (values.empty())
    {
      return 0;
    }

    std::sort(values.begin(), values.end());

    if (std::isnan(p))
    {
      return values.front();
    }

    const auto n = static_cast<double>(values.size());
    const auto rank = std::clamp(std::ceil(p * n / 100.0), 1.0, n);
    return values[static_cast<std::size_t>(rank) - 1];
  }

  bool isHitch(const uint32_t micros, const uint32_t medianMicros)
  {
    return medianMicros > 0 && static_cast<uint64_t>(micros) > 2ull * medianMicros;
  }

  Summary summarizeWall(const History& history, const std::size_t window)
  {
    return summarize(windowValues(history, window, [](const Sample& sample) { return sample.wallMicros; }));
  }

  Summary summarizeNode(const History& history, const NodeId node, const std::size_t window)
  {
    if (node >= maxNodes)
    {
      return {};
    }

    return summarize(windowValues(history, window, [node](const Sample& sample) { return sample.nodeMicros[node]; }));
  }

  std::size_t countHitches(const History& history, const std::size_t window)
  {
    const auto values = windowValues(history, window, [](const Sample& sample) { return sample.wallMicros; });
    const auto median = percentile(values, 50.0);

    return static_cast<std::size_t>(
      std::count_if(values.begin(), values.end(), [median](const uint32_t micros) { return isHitch(micros, median); }));
  }
}
