#ifndef PROFILERCHARTLAYOUT_H
#define PROFILERCHARTLAYOUT_H

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace profilerChart {
  struct ColumnSpan {
    float left = 0.0f;
    float right = 0.0f;
  };

  // Neighboring columns share an edge, so the spans cover [0, width] with no gap and no overlap however the
  // width divides.
  [[nodiscard]] inline ColumnSpan columnSpan(const std::size_t index, const std::size_t count, const float width)
  {
    if (count == 0 || index >= count)
    {
      return {};
    }

    const auto edge = [&](const std::size_t i) {
      return std::round(width * static_cast<float>(i) / static_cast<float>(count));
    };

    return { edge(index), edge(index + 1) };
  }

  // Pixels of bar for a duration against the scale, never above the chart height.
  [[nodiscard]] inline float barHeight(const double micros, const double scaleMillis, const float chartHeight)
  {
    if (scaleMillis <= 0.0 || micros <= 0.0)
    {
      return 0.0f;
    }

    const double fraction = micros / (scaleMillis * 1000.0);

    return static_cast<float>(std::min(fraction, 1.0)) * chartHeight;
  }

  // Extra left offset of a breakdown row; strictly increasing with depth, zero at depth 0.
  [[nodiscard]] inline float rowIndent(const std::size_t depth)
  {
    return static_cast<float>(depth) * 14.0f;
  }

  [[nodiscard]] inline bool exceedsScale(const double micros, const double scaleMillis)
  {
    return scaleMillis > 0.0 && micros > scaleMillis * 1000.0;
  }
}

#endif
