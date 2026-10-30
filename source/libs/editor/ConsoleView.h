#ifndef CONSOLEVIEW_H
#define CONSOLEVIEW_H

#include <LogEntry.h>
#include <LogFilter.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// What the Console window shows of a ring buffer snapshot: the rows that pass the filter, and how many
// uncleared entries each level holds regardless of the filter, so a hidden level still shows its count.
struct ConsoleView {
  std::vector<LogEntry> rows;
  std::array<int, levelCount> levelCounts{};
};

// retained is RingBufferSink::snapshot() taken alongside currentSequence (RingBufferSink::sequence());
// entries at or before clearedBefore are hidden.
[[nodiscard]] inline ConsoleView buildConsoleView(const std::vector<LogEntry>& retained,
                                                  const std::uint64_t currentSequence,
                                                  const std::uint64_t clearedBefore, const LogFilter& filter)
{
  const auto count = retained.size();

  // The sink does not tag entries with their write sequence, so the first retained entry's sequence is
  // derived from the current total and the retained count. A write racing this pair of calls can shift it
  // by one; that only risks a just-cleared or just-kept entry landing on the wrong side of the line for a
  // single frame, which is not worth a lock shared with the sink to avoid.
  const std::uint64_t firstSequence = currentSequence >= count
    ? currentSequence - static_cast<std::uint64_t>(count) + 1
    : 1;

  ConsoleView view;
  view.rows.reserve(count);

  for (std::size_t i = 0; i < count; ++i)
  {
    const std::uint64_t entrySequence = firstSequence + i;
    if (entrySequence <= clearedBefore)
    {
      continue;
    }

    ++view.levelCounts[static_cast<std::size_t>(retained[i].level)];

    if (filter.matches(retained[i]))
    {
      view.rows.push_back(retained[i]);
    }
  }

  return view;
}

#endif  // CONSOLEVIEW_H
