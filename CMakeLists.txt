cmake_minimum_required(VERSION 3.16)
project(farmos LANGUAGES CXX C VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Only set CMAKE_BUILD_TYPE for single-config generators (Ninja/Makefiles).
# Multi-config (Visual Studio) uses --config / ctest -C instead.
if(NOT CMAKE_CONFIGURATION_TYPES)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  endif()
endif()

add_executable(farmc
  src/main.cpp
  src/lexer.cpp
  src/parser.cpp
  src/sema.cpp
  src/emit_c.cpp
)
target_include_directories(farmc PRIVATE src runtime)

add_custom_command(TARGET farmc POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:farmc>/runtime
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/runtime/farm_rt.c
    ${CMAKE_SOURCE_DIR}/runtime/farm_rt.h
    $<TARGET_FILE_DIR:farmc>/runtime/
)

enable_testing()

set(FARMC_TESTS_DIR "${CMAKE_SOURCE_DIR}/spec/tests/M1")
file(GLOB M1_EXPECTED "${FARMC_TESTS_DIR}/*.expected")
foreach(exp ${M1_EXPECTED})
  get_filename_component(name ${exp} NAME_WE)
  add_test(NAME m1_${name}
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/run_one_m1.ps1"
            -Farmc $<TARGET_FILE:farmc>
            -TestsDir "${FARMC_TESTS_DIR}"
            -Name "${name}"
  )
  set_tests_properties(m1_${name} PROPERTIES LABELS "spec")
endforeach()

option(FARMC_ENABLE_CONFORMANCE_BUNDLE "Register m1_conformance umbrella test" ON)
if(FARMC_ENABLE_CONFORMANCE_BUNDLE)
  add_test(NAME m1_conformance
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/run_m1_tests.ps1"
            -Farmc $<TARGET_FILE:farmc>
            -TestsDir "${FARMC_TESTS_DIR}"
  )
  set_tests_properties(m1_conformance PROPERTIES LABELS "conformance_bundle")
endif()

set(FARMC_LOCAL_TESTS_DIR "${CMAKE_SOURCE_DIR}/tests/local")
file(GLOB LOCAL_EXPECTED "${FARMC_LOCAL_TESTS_DIR}/*.expected")
foreach(exp ${LOCAL_EXPECTED})
  get_filename_component(name ${exp} NAME_WE)
  # Negative proofs are registered separately (expect runner failure).
  if(name STREQUAL "040_trap_wrong_stderr" OR name STREQUAL "041_run_unexpected_stderr")
    # skip positive registration
  else()
    add_test(NAME local_${name}
      COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/run_one_m1.ps1"
              -Farmc $<TARGET_FILE:farmc>
              -TestsDir "${FARMC_LOCAL_TESTS_DIR}"
              -Name "${name}"
    )
    set_tests_properties(local_${name} PROPERTIES LABELS "local")
  endif()
endforeach()

add_test(NAME local_030_farm_cc_cl
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/test_farm_cc_cl.ps1"
          -Farmc $<TARGET_FILE:farmc>
)
set_tests_properties(local_030_farm_cc_cl PROPERTIES LABELS "local")

add_test(NAME local_031_farm_cc_cl_fullpath
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/test_farm_cc_cl_fullpath.ps1"
          -Farmc $<TARGET_FILE:farmc>
)
set_tests_properties(local_031_farm_cc_cl_fullpath PROPERTIES LABELS "local")

add_test(NAME local_040_trap_wrong_stderr
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/expect_runner_fail.ps1"
          -Farmc $<TARGET_FILE:farmc>
          -TestsDir "${FARMC_LOCAL_TESTS_DIR}"
          -Name "040_trap_wrong_stderr"
          -ExpectSubstring "stderr mismatch"
)
set_tests_properties(local_040_trap_wrong_stderr PROPERTIES LABELS "local")

add_test(NAME local_041_run_unexpected_stderr
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/expect_runner_fail.ps1"
          -Farmc $<TARGET_FILE:farmc>
          -TestsDir "${FARMC_LOCAL_TESTS_DIR}"
          -Name "041_run_unexpected_stderr"
          -ExpectSubstring "unexpected stderr"
)
set_tests_properties(local_041_run_unexpected_stderr PROPERTIES LABELS "local")
