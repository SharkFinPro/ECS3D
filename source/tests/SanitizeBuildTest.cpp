#include <gtest/gtest.h>

// Decided by the preprocessor rather than by constexpr flags: each build compiles only the branch it can
// reach, so no condition in the test bodies is constant.
#if defined(__SANITIZE_ADDRESS__)
#define ECS3D_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ECS3D_TEST_ASAN 1
#endif
#endif

#ifndef ECS3D_TEST_ASAN
#define ECS3D_TEST_ASAN 0
#endif

TEST(SanitizeBuild, InstrumentedCodeIsOptimizedSoTheSuiteStaysFast)
{
#if !ECS3D_TEST_ASAN || defined(_MSC_VER)
  GTEST_SKIP() << "Not a GCC or Clang sanitized build.";
#elif !defined(__OPTIMIZE__)
  ADD_FAILURE() << "A sanitized GCC or Clang build compiles without optimization.";
#endif
}

TEST(SanitizeBuild, InstrumentedCodeKeepsAssertsLive)
{
#if !ECS3D_TEST_ASAN
  GTEST_SKIP() << "Not a sanitized build.";
#elif defined(NDEBUG)
  ADD_FAILURE() << "A sanitized build defines NDEBUG, so assert() is compiled out.";
#endif
}
