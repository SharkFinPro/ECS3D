# Checks RegisterTestSuites.cmake against a canned `--gtest_list_tests` listing.
#
# Run with -P. Inputs (-D): REGISTER_SCRIPT (the script under test) and WORK_DIR (scratch directory).

if(NOT REGISTER_SCRIPT OR NOT WORK_DIR)
  message(FATAL_ERROR "REGISTER_SCRIPT and WORK_DIR are required")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")

set(listing
"Running main() from /build/_deps/googletest-src/googletest/src/gtest_main.cc
PlainSuite.
  First
  Second
Library/ShapeDrop.  # TypeParam =
  Falls/0  # GetParam() = (8, 0)
  Falls/1  # GetParam() = (16, 1)
Typed/0.  # TypeParam = float
  Works
Typed/1.  # TypeParam = int
  Works
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

run_script("${listing}" suites.cmake)
if(NOT scriptResult EQUAL 0)
  message(FATAL_ERROR "The script failed on a valid listing: ${scriptOutput}")
endif()

file(READ "${WORK_DIR}/suites.cmake" generated)

set(expectedSuites PlainSuite Library/ShapeDrop Typed/0 Typed/1 Mixed)
foreach(suite IN LISTS expectedSuites)
  set(expectedLine
    "add_test([==[${suite}]==] [==[/path/to/ECS3DTests]==] [==[--gtest_filter=${suite}.*]==] [==[--gtest_brief=1]==])\n")
  string(FIND "${generated}" "${expectedLine}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing registration for ${suite} in:\n${generated}")
  endif()
endforeach()

string(FIND "${generated}" "WORKING_DIRECTORY [==[/path/to/work dir]==]" position)
if(position EQUAL -1)
  message(FATAL_ERROR "Missing working directory in:\n${generated}")
endif()

# The count is the positive control: it fails whether the parser returns everything or nothing.
string(REGEX MATCHALL "add_test[(]" registrations "${generated}")
list(LENGTH registrations registrationCount)
list(LENGTH expectedSuites expectedCount)
if(NOT registrationCount EQUAL expectedCount)
  message(FATAL_ERROR "Expected ${expectedCount} registrations, got ${registrationCount}:\n${generated}")
endif()

foreach(absent DISABLED_Whole OnlyDisabledCases Inner Banner main gtest_main)
  string(FIND "${generated}" "${absent}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "${absent} must not be registered:\n${generated}")
  endif()
endforeach()

foreach(emptyListing "" "Running main() from gtest_main.cc\n" "DISABLED_Only.\n  Case\n")
  run_script("${emptyListing}" empty.cmake)
  if(scriptResult EQUAL 0)
    message(FATAL_ERROR "The script accepted a listing with no runnable suites: [${emptyListing}]")
  endif()
  if(EXISTS "${WORK_DIR}/empty.cmake")
    message(FATAL_ERROR "A failed run left an output file behind")
  endif()
endforeach()
