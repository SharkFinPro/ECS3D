# Writes a CTest file that registers one test per GoogleTest suite, from `--gtest_list_tests` output.
#
# Run with -P. Inputs (-D):
#   TEST_EXECUTABLE   the GoogleTest binary to list, or
#   TEST_LIST_FILE    a file holding canned listing text (used instead of the binary when set)
#   OUTPUT_FILE       the CTest file to write
#   WORKING_DIRECTORY the directory the registered tests run in
#   TIMEOUT           seconds to allow the listing run (default 120)

# Returns the names of the suites that have at least one test GoogleTest will run. A suite whose name or
# cases are all DISABLED_ is left out: a filter matching only disabled tests runs nothing and passes.
# An unindented line that is neither a suite header nor gtest_main's banner is an error, because skipping
# an unparseable header would credit its cases to the previous suite.
function(ecs3d_parse_test_suites LISTING OUT_VAR)
  # Comments such as "# GetParam() = (8, 0)" can hold list separators, so they go first.
  string(REGEX REPLACE "[ \t]+#[^\n]*" "" text "${LISTING}")
  string(REPLACE "\r" "" text "${text}")
  string(REPLACE ";" "_" text "${text}")
  string(REPLACE "[" "_" text "${text}")
  string(REPLACE "]" "_" text "${text}")
  string(REPLACE "\\" "_" text "${text}")
  string(REPLACE "\n" ";" lines "${text}")

  set(suites "")
  set(current "")
  set(haveSuite FALSE)
  set(hasEnabledCase FALSE)

  foreach(line IN LISTS lines)
    string(REGEX REPLACE "[ \t]+$" "" line "${line}")

    if(line STREQUAL "")
      continue()
    endif()

    if(line MATCHES "^[ \t]")
      if(haveSuite AND line MATCHES "^[ \t]+([A-Za-z0-9_/]+)$")
        if(NOT CMAKE_MATCH_1 MATCHES "^DISABLED_")
          set(hasEnabledCase TRUE)
        endif()
      endif()
    elseif(line MATCHES "^([A-Za-z0-9_/]+)\\.$")
      if(haveSuite AND hasEnabledCase)
        list(APPEND suites "${current}")
      endif()

      set(current "${CMAKE_MATCH_1}")
      set(haveSuite TRUE)
      set(hasEnabledCase FALSE)

      string(REPLACE "/" ";" components "${current}")
      foreach(component IN LISTS components)
        if(component MATCHES "^DISABLED_")
          set(haveSuite FALSE)
        endif()
      endforeach()
    elseif(NOT line MATCHES "^Running main\\(\\) from ")
      message(FATAL_ERROR "Unrecognized line in the GoogleTest listing: [${line}]")
    endif()
  endforeach()

  if(haveSuite AND hasEnabledCase)
    list(APPEND suites "${current}")
  endif()

  set(${OUT_VAR} "${suites}" PARENT_SCOPE)
endfunction()

function(ecs3d_format_suite_tests SUITES EXECUTABLE WORKING_DIR OUT_VAR)
  set(content "")
  foreach(suite IN LISTS SUITES)
    string(APPEND content
      "add_test([==[${suite}]==] [==[${EXECUTABLE}]==] [==[--gtest_filter=${suite}.*]==] [==[--gtest_brief=1]==])\n"
      "set_tests_properties([==[${suite}]==] PROPERTIES WORKING_DIRECTORY [==[${WORKING_DIR}]==])\n")
  endforeach()
  set(${OUT_VAR} "${content}" PARENT_SCOPE)
endfunction()

function(ecs3d_register_test_suites)
  if(NOT OUTPUT_FILE OR NOT WORKING_DIRECTORY)
    message(FATAL_ERROR "RegisterTestSuites: OUTPUT_FILE and WORKING_DIRECTORY are required")
  endif()
  if(NOT TIMEOUT)
    set(TIMEOUT 120)
  endif()

  if(TEST_LIST_FILE)
    file(READ "${TEST_LIST_FILE}" listing)
    set(source "${TEST_LIST_FILE}")
    set(executable "${TEST_EXECUTABLE}")
  elseif(TEST_EXECUTABLE)
    execute_process(
      COMMAND "${TEST_EXECUTABLE}" --gtest_list_tests
      WORKING_DIRECTORY "${WORKING_DIRECTORY}"
      TIMEOUT ${TIMEOUT}
      RESULT_VARIABLE result
      OUTPUT_VARIABLE listing
      ERROR_VARIABLE errors
    )
    if(NOT "${result}" STREQUAL "0")
      message(FATAL_ERROR
        "Listing GoogleTest suites failed (${result}): ${TEST_EXECUTABLE} --gtest_list_tests\n"
        "stdout:\n${listing}\nstderr:\n${errors}")
    endif()
    set(source "${TEST_EXECUTABLE}")
    set(executable "${TEST_EXECUTABLE}")
  else()
    message(FATAL_ERROR "RegisterTestSuites: TEST_EXECUTABLE or TEST_LIST_FILE is required")
  endif()

  ecs3d_parse_test_suites("${listing}" suites)
  if(NOT suites)
    message(FATAL_ERROR "No runnable GoogleTest suites found in the listing from ${source}:\n${listing}")
  endif()

  ecs3d_format_suite_tests("${suites}" "${executable}" "${WORKING_DIRECTORY}" content)

  # A failed run must not leave a half-written file for ctest to include.
  set(temp "${OUTPUT_FILE}.tmp")
  file(WRITE "${temp}" "${content}")
  file(RENAME "${temp}" "${OUTPUT_FILE}")
endfunction()

if(CMAKE_CURRENT_LIST_FILE STREQUAL CMAKE_SCRIPT_MODE_FILE)
  ecs3d_register_test_suites()
endif()
