#ifndef EDITTOKEN_H
#define EDITTOKEN_H

#include <cstddef>
#include <random>
#include <string>

// The token an editor presents to an edit server. The server generates one when --edit has no --token, and
// the editor generates one for the server it spawns.
namespace net {

template <class Generator>
[[nodiscard]] std::string generateEditToken(Generator& generator)
{
  constexpr std::size_t byteCount = 16;
  constexpr char hexDigits[] = "0123456789abcdef";

  std::uniform_int_distribution<unsigned int> byteDistribution(0, 255);

  std::string token;
  token.reserve(byteCount * 2);
  for (std::size_t i = 0; i < byteCount; ++i)
  {
    const unsigned int value = byteDistribution(generator);
    token += hexDigits[value >> 4];
    token += hexDigits[value & 0xFu];
  }

  return token;
}

// Every byte comes from random_device rather than a seeded engine, since an engine seeded with one 32-bit
// value can only produce 2^32 tokens.
[[nodiscard]] inline std::string generateEditToken()
{
  std::random_device generator;
  return generateEditToken(generator);
}

} // namespace net

#endif //EDITTOKEN_H
