#ifndef DASHEDBOXLAYOUT_H
#define DASHEDBOXLAYOUT_H

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Greedy word wrap of `text` into lines no wider than `maxWidth` as reported by `measure(std::string_view)`.
// A word wider than the limit is split by characters; every line holds at least one character. Splits on spaces
// only and collapses runs of them, so it expects single-line ASCII: a character split would cut a multi-byte
// code point.
template <typename Measure>
[[nodiscard]] std::vector<std::string> wrapTextLines(const std::string_view text, const float maxWidth,
                                                     Measure&& measure)
{
  std::vector<std::string> lines;
  std::string line;

  const auto flush = [&]()
  {
    if (!line.empty())
    {
      lines.push_back(line);
      line.clear();
    }
  };

  size_t pos = 0;
  while (pos < text.size())
  {
    const size_t end = std::min(text.find(' ', pos), text.size());
    std::string_view word = text.substr(pos, end - pos);
    pos = end < text.size() ? end + 1 : end;

    if (word.empty())
    {
      continue;
    }

    const std::string joined = line.empty() ? std::string(word) : line + ' ' + std::string(word);
    if (measure(std::string_view(joined)) <= maxWidth)
    {
      line = joined;
      continue;
    }

    flush();

    while (measure(word) > maxWidth && word.size() > 1)
    {
      size_t fit = 1;
      while (fit < word.size() && measure(word.substr(0, fit + 1)) <= maxWidth)
      {
        ++fit;
      }

      lines.emplace_back(word.substr(0, fit));
      word.remove_prefix(fit);
    }

    line = std::string(word);
  }

  flush();

  return lines;
}

// The box grows past `minHeight` only when the wrapped lines plus padding need more room.
[[nodiscard]] inline float dashedBoxHeight(const size_t lineCount, const float lineHeight, const float padding,
                                           const float minHeight)
{
  return std::max(minHeight, static_cast<float>(lineCount) * lineHeight + 2.0f * padding);
}

#endif
