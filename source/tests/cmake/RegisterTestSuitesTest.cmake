# Checks RegisterTestSuites.cmake against canned `--gtest_list_tests` listings.
#
# Run with -P. Inputs (-D): REGISTER_SCRIPT (the script under test) and WORK_DIR (scratch directory).

if(NOT REGISTER_SCRIPT OR NOT WORK_DIR)
  message(FATAL_ERROR "REGISTER_SCRIPT and WORK_DIR are required")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")

string(ASCII 92 bs)

# The comments carry list separators, brackets, quotes and a trailing backslash.
set(listing
"Running main() from /build/_deps/googletest-src/googletest/src/gtest_main.cc
PlainSuite.
  First
  Second
Library/ShapeDrop.  # TypeParam =
  Falls/0  # GetParam() = (8, 0)
  Falls/1  # GetParam() = (16, 1)
  Falls/2  # GetParam() = (\"a;b\", [x], 'q', C:${bs}dir${bs}
Typed/0.  # TypeParam = float
  Works
Typed/1.  # TypeParam = int
  Works
OFF.
  Case
NO.
  Case
DISABLED_Whole.
  Case
Mixed.
  DISABLED_Off
  On
OnlyDisabledCases.
  DISABLED_One
  DISABLED_Two
Prefix/DISABLED_Inner.
  Case/0  # GetParam() = 1
")

function(run_script listingText outName)
  file(WRITE "${WORK_DIR}/listing.txt" "${listingText}")
  file(REMOVE "${WORK_DIR}/${outName}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      -D "TEST_LIST_FILE=${WORK_DIR}/listing.txt"
      -D "TEST_EXECUTABLE=/path/to/ECS3DTests"
      -D "OUTPUT_FILE=${WORK_DIR}/${outName}"
      -D "WORKING_DIRECTORY=/path/to/work dir"
      -P "${REGISTER_SCRIPT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
  )
  set(scriptResult "${result}" PARENT_SCOPE)
  set(scriptOutput "${out}${err}" PARENT_SCOPE)
endfunction()

function(expect_registered label listingText)
  run_script("${listingText}" suites.cmake)
  if(NOT scriptResult EQUAL 0)
    message(FATAL_ERROR "[${label}] The script failed on a valid listing: ${scriptOutput}")
  endif()

  file(READ "${WORK_DIR}/suites.cmake" generated)

  set(expectedSuites PlainSuite Library/ShapeDrop Typed/0 Typed/1 OFF NO Mixed)
  foreach(suite IN LISTS expectedSuites)
    set(expectedLine
      "add_test([==[${suite}]==] [==[/path/to/ECS3DTests]==] [==[--gtest_filter=${suite}.*]==] [==[--gtest_brief=1]==])\n")
    string(FIND "${generated}" "${expectedLine}" position)
    if(position EQUAL -1)
      message(FATAL_ERROR "[${label}] Missing registration for ${suite} in:\n${generated}")
    endif()
  endforeach()

  string(FIND "${generated}" "WORKING_DIRECTORY [==[/path/to/work dir]==]" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "[${label}] Missing working directory in:\n${generated}")
  endif()

  # The count is the positive control: it fails whether the parser returns everything or nothing.
  string(REGEX MATCHALL "add_test[(]" registrations "${generated}")
  list(LENGTH registrations registrationCount)
  list(LENGTH expectedSuites expectedCount)
  if(NOT registrationCount EQUAL expectedCount)
    message(FATAL_ERROR "[${label}] Expected ${expectedCount} registrations, got ${registrationCount}:\n${generated}")
  endif()

  # Case names and disabled suites must not become suites of their own.
  foreach(absent DISABLED_Whole OnlyDisabledCases Inner Falls First)
    string(FIND "${generated}" "[==[${absent}" position)
    if(NOT position EQUAL -1)
      message(FATAL_ERROR "[${label}] ${absent} must not be registered:\n${generated}")
    endif()
  endforeach()
endfunction()

function(expect_failure label listingText reason)
  run_script("${listingText}" failed.cmake)
  if(scriptResult EQUAL 0)
    message(FATAL_ERROR "[${label}] The script accepted a bad listing")
  endif()
  string(FIND "${scriptOutput}" "${reason}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "[${label}] The error does not mention '${reason}':\n${scriptOutput}")
  endif()
  if(EXISTS "${WORK_DIR}/failed.cmake")
    message(FATAL_ERROR "[${label}] A failed run left an output file behind")
  endif()
endfunction()

expect_registered("lf" "${listing}")

string(REPLACE "\n" "\r\n" crlfListing "${listing}")
expect_registered("crlf" "${crlfListing}")

string(REGEX REPLACE "\n$" "" noNewlineListing "${listing}")
expect_registered("no trailing newline" "${noNewlineListing}")

expect_failure("empty" "" "No runnable")
expect_failure("banner only" "Running main() from gtest_main.cc\n" "No runnable")
expect_failure("disabled only" "DISABLED_Only.\n  Case\n" "No runnable")

expect_failure("stray word" "Stray line\nPlainSuite.\n  Case\n" "Stray line")
expect_failure("short token" "PlainSuite.\n  Case\nOk\n" "Ok")
expect_failure("bracketed line" "PlainSuite.\n  Case\n[info] x\n" "info")
expect_failure("bad header" "PlainSuite.\n  Case\nBad-Name.\n  Case\n" "Bad-Name")
