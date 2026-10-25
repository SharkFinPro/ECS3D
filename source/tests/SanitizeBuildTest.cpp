#include <gtest/gtest.h>

namespace {
#if defined(__SANITIZE_ADDRESS__)
  constexpr bool sanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
  constexpr bool sanitized = true;
#else
  constexpr bool sanitized = false;
#endif
#else
  constexpr bool sanitized = false;
#endif

#if defined(_MSC_VER)
  constexpr bool isMsvc = true;
#else
  constexpr bool isMsvc = false;
#endif

#if defined(__OPTIMIZE__)
  constexpr bool optimized = true;
#else
  constexpr bool optimized = false;
#endif

#if defined(NDEBUG)
  constexpr bool assertsDisabled = true;
#else
  constexpr bool assertsDisabled = false;
#endif
}

TEST(SanitizeBuild, InstrumentedCodeIsOptimizedSoTheSuiteStaysFast)
{
  if (!sanitized || isMsvc)
  {
    GTEST_SKIP() << "Not a GCC or Clang sanitized build.";
  }

  EXPECT_TRUE(optimized);
}

TEST(SanitizeBuild, InstrumentedCodeKeepsAssertsLive)
{
  if (!sanitized)
  {
    GTEST_SKIP() << "Not a sanitized build.";
  }

  EXPECT_FALSE(assertsDisabled);
}
