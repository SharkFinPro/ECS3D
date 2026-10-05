#ifndef WIRETRUNCATION_H
#define WIRETRUNCATION_H

#include <gtest/gtest.h>

#include <Protocol.h>
#include <cstddef>
#include <string>

// Helpers for cutting a valid wire message at every offset. A bounds check missing from one field passes
// any test whose cut lands elsewhere, so the sweep tries them all.
namespace wiretest {
  // The message's type with only its first `length` payload bytes.
  [[nodiscard]] inline net::Message prefixOf(const net::Message& source, const std::size_t length)
  {
    return net::Message(source.getType(), source.bytes().first(length));
  }

  // Calls check(prefix, length) for every proper prefix, from the empty payload up to one byte short of
  // the whole message, each under a trace naming the length.
  template <typename Check>
  void forEachProperPrefix(const net::Message& source, Check&& check)
  {
    for (std::size_t length = 0; length < source.size(); ++length)
    {
      SCOPED_TRACE("payload cut to " + std::to_string(length) + " of " + std::to_string(source.size()) + " bytes");
      check(prefixOf(source, length), length);
    }
  }
}

#endif //WIRETRUNCATION_H
