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

# Runtime (farm_rt.* + vendored ryu/) next to farmc for default_runtime_dir discovery.
add_custom_command(TARGET farmc POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/runtime $<TARGET_FILE_DIR:farmc>/runtime
)

enable_testing()

set(PS powershell -NoProfile -ExecutionPolicy Bypass -File)
set(FARMC_SCRIPTS "${CMAKE_SOURCE_DIR}/scripts")

# ---- Spec fixtures (spec/tests/M1) ----
set(FARMC_TESTS_DIR "${CMAKE_SOURCE_DIR}/spec/tests/M1")
file(GLOB M1_EXPECTED "${FARMC_TESTS_DIR}/*.expected")
foreach(exp ${M1_EXPECTED})
  get_filename_component(name ${exp} NAME_WE)
  add_test(NAME m1_${name}
    COMMAND ${PS} "${FARMC_SCRIPTS}/run_one_m1.ps1" -Farmc $<TARGET_FILE:farmc> -TestsDir "${FARMC_TESTS_DIR}" -Name "${name}")
  set_tests_properties(m1_${name} PROPERTIES LABELS "spec")
endforeach()

# Opt-in umbrella: labeled conformance_bundle; default ctest uses -LE conformance_bundle.
option(FARMC_ENABLE_CONFORMANCE_BUNDLE "Register m1_conformance umbrella test" ON)
if(FARMC_ENABLE_CONFORMANCE_BUNDLE)
  add_test(NAME m1_conformance
    COMMAND ${PS} "${FARMC_SCRIPTS}/run_m1_tests.ps1" -Farmc $<TARGET_FILE:farmc> -TestsDir "${FARMC_TESTS_DIR}")
  set_tests_properties(m1_conformance PROPERTIES LABELS "conformance_bundle")
endif()

# ---- Local positive fixtures (tests/local) ----
set(FARMC_LOCAL_TESTS_DIR "${CMAKE_SOURCE_DIR}/tests/local")
file(GLOB LOCAL_EXPECTED "${FARMC_LOCAL_TESTS_DIR}/*.expected")
foreach(exp ${LOCAL_EXPECTED})
  get_filename_component(name ${exp} NAME_WE)
  add_test(NAME local_${name}
    COMMAND ${PS} "${FARMC_SCRIPTS}/run_one_m1.ps1" -Farmc $<TARGET_FILE:farmc> -TestsDir "${FARMC_LOCAL_TESTS_DIR}" -Name "${name}")
  set_tests_properties(local_${name} PROPERTIES LABELS "local")
endforeach()

# ---- Negative runner proofs (tests/negative): the runner MUST fail with a specific message ----
set(FARMC_NEG_TESTS_DIR "${CMAKE_SOURCE_DIR}/tests/negative")
function(farmc_neg_test name substr)
  add_test(NAME local_${name}
    COMMAND ${PS} "${FARMC_SCRIPTS}/expect_runner_fail.ps1" -Farmc $<TARGET_FILE:farmc> -TestsDir "${FARMC_NEG_TESTS_DIR}" -Name "${name}" -ExpectSubstring "${substr}")
  set_tests_properties(local_${name} PROPERTIES LABELS "local")
endfunction()
farmc_neg_test(040_trap_wrong_stderr "stderr mismatch")
farmc_neg_test(041_run_unexpected_stderr "unexpected stderr")
farmc_neg_test(042_stdout_case_only "stdout mismatch")
farmc_neg_test(043_stderr_case_only "stderr mismatch")

# ---- MSVC cl rejection (AC-M1-04, section 7.1) ----
function(farmc_cl_test name mode)
  add_test(NAME local_${name}
    COMMAND ${PS} "${FARMC_SCRIPTS}/test_farm_cc_cl.ps1" -Farmc $<TARGET_FILE:farmc> -Mode ${mode})
  set_tests_properties(local_${name} PROPERTIES LABELS "local")
endfunction()
farmc_cl_test(030_farm_cc_cl bare)
farmc_cl_test(031_farm_cc_cl_fullpath fullpath)
farmc_cl_test(033_farm_cc_cl_args args)
farmc_cl_test(034_farm_cc_cl_trailing_space trailing)
farmc_cl_test(035_farm_cc_cl_quoted_path_args quoted_path_args)
farmc_cl_test(036_farm_cc_cl_path_args path_args)
farmc_cl_test(037_farm_cc_cl_upper_cmd upper_cmd)

# ---- -o without .exe (section 7.1 SHOULD) ----
add_test(NAME local_032_o_noext COMMAND ${PS} "${FARMC_SCRIPTS}/test_o_noext.ps1" -Farmc $<TARGET_FILE:farmc>)
set_tests_properties(local_032_o_noext PROPERTIES LABELS "local")

# ---- UTF-8: exactly one E0001 per ill-formed file (section 2.1) ----
add_test(NAME local_060_e0001_single
  COMMAND ${PS} "${FARMC_SCRIPTS}/test_utf8_single.ps1" -Farmc $<TARGET_FILE:farmc> -TestsDir "${FARMC_LOCAL_TESTS_DIR}")
set_tests_properties(local_060_e0001_single PROPERTIES LABELS "local")

# ---- farmc exit codes (section 7.2) ----
function(farmc_exit_test name case)
  add_test(NAME local_${name} COMMAND ${PS} "${FARMC_SCRIPTS}/test_exit_codes.ps1" -Farmc $<TARGET_FILE:farmc> -Case ${case})
  set_tests_properties(local_${name} PROPERTIES LABELS "local")
endfunction()
farmc_exit_test(080_exit2_unknown_option usage_unknown_option)
farmc_exit_test(081_exit2_no_file usage_no_file)
farmc_exit_test(082_exit2_unknown_command usage_unknown_command)
farmc_exit_test(083_exit3_missing_input missing_input)
farmc_exit_test(084_exit4_ice ice)

# ---- Float formatting differential test vs Node String(x) (AC-M1-05, section 6.1) ----
# Quick variant in the default suite; the full 1,000,000-pattern run is `node scripts/float_diff.mjs`.
find_program(FARMC_NODE node)
if(FARMC_NODE)
  add_test(NAME local_090_float_diff_quick
    COMMAND ${FARMC_NODE} "${FARMC_SCRIPTS}/float_diff.mjs" --count 20000)
  set_tests_properties(local_090_float_diff_quick PROPERTIES LABELS "local")
endif()
