#ifndef SERVERARGUMENTS_H
#define SERVERARGUMENTS_H

#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace net {

// The --port flag (with its leading space) for a spawned local server, so the server listens on the port its
// parent connects to.
[[nodiscard]] inline std::string serverPortArgument(const int port)
{
  return " --port " + std::to_string(port);
}

// The value of a --port flag: a whole string of decimal digits naming a port from 0 to 65535 (0 lets the OS
// pick). Anything else - empty, signed, padded, trailing characters, out of range - is nullopt.
[[nodiscard]] inline std::optional<int> parsePortArgument(const std::string_view text)
{
  if (text.empty() || text.front() < '0' || text.front() > '9')
  {
    return std::nullopt;
  }

  int value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size() || value > 65535)
  {
    return std::nullopt;
  }

  return value;
}

// The value of a --stats-interval flag: a whole string of decimal digits naming seconds from 0 to 3600 (0 turns the
// summary off). Anything else is nullopt.
[[nodiscard]] inline std::optional<int> parseStatsIntervalArgument(const std::string_view text)
{
  if (text.empty() || text.front() < '0' || text.front() > '9')
  {
    return std::nullopt;
  }

  int value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size() || value > 3600)
  {
    return std::nullopt;
  }

  return value;
}

}

#endif //SERVERARGUMENTS_H
