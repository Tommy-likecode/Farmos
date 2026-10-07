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
  src/conflict.cpp
)
target_include_directories(farmc PRIVATE src runtime)

# Test-only build of the same sources with FARMC_ENABLE_TEST_HOOKS (FARMC_TEST_ICE -> exit 4).
# The shipped farmc is built WITHOUT the define and ignores FARMC_TEST_ICE (local_085).
add_executable(farmc_testhooks
  src/main.cpp
  src/lexer.cpp
  src/parser.cpp
  src/sema.cpp
  src/emit_c.cpp
  src/conflict.cpp
)
target_include_directories(farmc_testhooks PRIVATE src runtime)
target_compile_definitions(farmc_testhooks PRIVATE FARMC_ENABLE_TEST_HOOKS=1)

# Runtime (farm_rt.* + vendored ryu/) next to farmc for default_runtime_dir discovery.
add_custom_command(TARGET farmc POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/runtime $<TARGET_FILE_DIR:farmc>/runtime
)

enable_testing()

set(PS powershell -NoProfile -ExecutionPolicy Bypass -File)
set(FARMC_SCRIPTS "${CMAKE_SOURCE_DIR}/scripts")

# Windows: scripts/run_one_m1.ps1. Linux/macOS: scripts/run_one_linux.sh
# (same contract: `farmc build <main> -o <exe>` with cwd = fixtures dir).
function(farmc_add_one_test test_name tests_dir fixture_name)
  if(WIN32)
    add_test(NAME ${test_name}
      COMMAND ${PS} "${FARMC_SCRIPTS}/run_one_m1.ps1" -Farmc $<TARGET_FILE:farmc> -TestsDir "${tests_dir}" -Name "${fixture_name}")
  else()
    add_test(NAME ${test_name}
      COMMAND bash "${FARMC_SCRIPTS}/run_one_linux.sh" $<TARGET_FILE:farmc> "${tests_dir}" "${fixture_name}")
  endif()
endfunction()

# ---- Spec fixtures (spec/tests/M1) ----
set(FARMC_TESTS_DIR "${CMAKE_SOURCE_DIR}/spec/tests/M1")
file(GLOB M1_EXPECTED "${FARMC_TESTS_DIR}/*.expected")
foreach(exp ${M1_EXPECTED})
  get_filename_component(name ${exp} NAME_WE)
  # M2 note: m1_039_mixed_arith_error expects E0402 for `1 + 2.0`, but M2 §4A (OQ-M2-11)
  # requires int literal coercion in this case. This M1 test is superseded by M2 and excluded
  # from M2 builds to keep the official Windows test suite green.
  if(NOT (name STREQUAL "039_mixed_arith_error"))
    farmc_add_one_test(m1_${name} "${FARMC_TESTS_DIR}" "${name}")
    set_tests_properties(m1_${name} PROPERTIES LABELS "spec")
  endif()
endforeach()

# Opt-in umbrella: labeled conformance_bundle; default ctest uses -LE conformance_bundle.
option(FARMC_ENABLE_CONFORMANCE_BUNDLE "Register m1_conformance umbrella test" ON)
if(FARMC_ENABLE_CONFORMANCE_BUNDLE AND WIN32)
  add_test(NAME m1_conformance
    COMMAND ${PS} "${FARMC_SCRIPTS}/run_m1_tests.ps1" -Farmc $<TARGET_FILE:farmc> -TestsDir "${FARMC_TESTS_DIR}")
  set_tests_properties(m1_conformance PROPERTIES LABELS "conformance_bundle")
endif()

# ---- M2 Spec fixtures (spec/tests/M2) ----
set(FARMC_M2_TESTS_DIR "${CMAKE_SOURCE_DIR}/spec/tests/M2")
file(GLOB M2_EXPECTED "${FARMC_M2_TESTS_DIR}/*.expected")
foreach(exp ${M2_EXPECTED})
  get_filename_component(name ${exp} NAME_WE)
  farmc_add_one_test(m2_${name} "${FARMC_M2_TESTS_DIR}" "${name}")
  set_tests_properties(m2_${name} PROPERTIES LABELS "spec")
endforeach()

# ---- M3 Spec fixtures (spec/tests/M3) ----
set(FARMC_M3_TESTS_DIR "${CMAKE_SOURCE_DIR}/spec/tests/M3")
file(GLOB M3_EXPECTED "${FARMC_M3_TESTS_DIR}/*.expected")
foreach(exp ${M3_EXPECTED})
  get_filename_component(name ${exp} NAME_WE)
  farmc_add_one_test(m3_${name} "${FARMC_M3_TESTS_DIR}" "${name}")
  set_tests_properties(m3_${name} PROPERTIES LABELS "spec")
endforeach()

# ---- M3.5 Spec fixtures (spec/tests/M3.5) ----
set(FARMC_M35_TESTS_DIR "${CMAKE_SOURCE_DIR}/spec/tests/M3.5")
file(GLOB M35_EXPECTED "${FARMC_M35_TESTS_DIR}/*.expected")
foreach(exp ${M35_EXPECTED})
  get_filename_component(name ${exp} NAME_WE)
  farmc_add_one_test(m35_${name} "${FARMC_M35_TESTS_DIR}" "${name}")
  set_tests_properties(m35_${name} PROPERTIES LABELS "spec")
endforeach()

# AC-M3-01 / AC-M3-11: unused farmos:scene import DCE + M1/M2 link flags vs master.
# Windows: PowerShell (no python3 required). Unix: python3.
find_program(FARMC_PYTHON3 python3)
if(WIN32)
  add_test(NAME local_unused_scene_dce
    COMMAND ${PS} "${FARMC_SCRIPTS}/test_unused_scene_dce.ps1"
            -Farmc $<TARGET_FILE:farmc> -RepoRoot "${CMAKE_SOURCE_DIR}")
  set_tests_properties(local_unused_scene_dce PROPERTIES LABELS "local")
elseif(FARMC_PYTHON3)
  add_test(NAME local_unused_scene_dce
    COMMAND ${FARMC_PYTHON3} "${FARMC_SCRIPTS}/test_unused_scene_dce.py"
            $<TARGET_FILE:farmc> "${CMAKE_SOURCE_DIR}")
  set_tests_properties(local_unused_scene_dce PROPERTIES LABELS "local")
endif()

# ---- Local positive fixtures (tests/local) ----
set(FARMC_LOCAL_TESTS_DIR "${CMAKE_SOURCE_DIR}/tests/local")
file(GLOB LOCAL_EXPECTED "${FARMC_LOCAL_TESTS_DIR}/*.expected")
foreach(exp ${LOCAL_EXPECTED})
  get_filename_component(name ${exp} NAME_WE)
  farmc_add_one_test(local_${name} "${FARMC_LOCAL_TESTS_DIR}" "${name}")
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
# Unquoted real-clang path under a "cl tools" directory must NOT be mistaken for MSVC cl.
foreach(mode whole args)
  add_test(NAME local_038_farm_cc_clang_in_cl_dir_${mode}
    COMMAND ${PS} "${FARMC_SCRIPTS}/test_farm_cc_cl_dir.ps1" -Farmc $<TARGET_FILE:farmc> -Mode ${mode})
  set_tests_properties(local_038_farm_cc_clang_in_cl_dir_${mode} PROPERTIES LABELS "local")
endforeach()

# ---- -o without .exe (section 7.1 SHOULD) ----
add_test(NAME local_032_o_noext COMMAND ${PS} "${FARMC_SCRIPTS}/test_o_noext.ps1" -Farmc $<TARGET_FILE:farmc>)
set_tests_properties(local_032_o_noext PROPERTIES LABELS "local")

# ---- UTF-8: exactly one E0001 per ill-formed file (section 2.1) ----
add_test(NAME local_060_e0001_single
  COMMAND ${PS} "${FARMC_SCRIPTS}/test_single_diag.ps1" -Farmc $<TARGET_FILE:farmc> -TestsDir "${FARMC_LOCAL_TESTS_DIR}"
    -Filter "05*_utf8_*.expected" -Code E0001 -MinCount 9 -TestName local_060_e0001_single)
set_tests_properties(local_060_e0001_single PROPERTIES LABELS "local")

# ---- Valid non-ASCII outside strings/comments: exactly one E0202, never E0001 ----
add_test(NAME local_124_e0202_single
  COMMAND ${PS} "${FARMC_SCRIPTS}/test_single_diag.ps1" -Farmc $<TARGET_FILE:farmc> -TestsDir "${FARMC_LOCAL_TESTS_DIR}"
    -Filter "12?_e0202_*.expected" -Code E0202 -MinCount 4 -TestName local_124_e0202_single)
set_tests_properties(local_124_e0202_single PROPERTIES LABELS "local")

# ---- String literal codegen: raw stdout bytes (non-ASCII, DEL, NUL, C specials) ----
add_test(NAME local_110_stdout_bytes_exact
  COMMAND ${PS} "${FARMC_SCRIPTS}/test_stdout_bytes.ps1" -Farmc $<TARGET_FILE:farmc> -TestsDir "${FARMC_LOCAL_TESTS_DIR}"
    -Filter "10?_*.expected" -MinCount 10)
set_tests_properties(local_110_stdout_bytes_exact PROPERTIES LABELS "local")

# ---- Concurrent same-stem builds: unique per-process temp names, no leftovers ----
add_test(NAME local_140_unicode_temp
  COMMAND ${PS} "${FARMC_SCRIPTS}/test_unicode_temp.ps1" -Farmc $<TARGET_FILE:farmc>)
set_tests_properties(local_140_unicode_temp PROPERTIES LABELS "local")

add_test(NAME local_130_parallel_same_stem
  COMMAND ${PS} "${FARMC_SCRIPTS}/test_parallel_builds.ps1" -Farmc $<TARGET_FILE:farmc> -Rounds 20 -RunRounds 5)
set_tests_properties(local_130_parallel_same_stem PROPERTIES LABELS "local")

# ---- farmc exit codes (section 7.2) ----
function(farmc_exit_test name case)
  add_test(NAME local_${name} COMMAND ${PS} "${FARMC_SCRIPTS}/test_exit_codes.ps1" -Farmc $<TARGET_FILE:farmc> -Case ${case})
  set_tests_properties(local_${name} PROPERTIES LABELS "local")
endfunction()
farmc_exit_test(080_exit2_unknown_option usage_unknown_option)
farmc_exit_test(081_exit2_no_file usage_no_file)
farmc_exit_test(082_exit2_unknown_command usage_unknown_command)
farmc_exit_test(083_exit3_missing_input missing_input)
add_test(NAME local_084_exit4_ice COMMAND ${PS} "${FARMC_SCRIPTS}/test_exit_codes.ps1" -Farmc $<TARGET_FILE:farmc_testhooks> -Case ice)
set_tests_properties(local_084_exit4_ice PROPERTIES LABELS "local")
farmc_exit_test(085_release_ignores_ice ice_ignored)
farmc_exit_test(086_o_existing_dir o_existing_dir)

# ---- Float formatting differential test vs Node String(x) (AC-M1-05, section 6.1) ----
# Quick variant in the default suite; the full 1,000,000-pattern run is `node scripts/float_diff.mjs`.
find_program(FARMC_NODE node)
if(FARMC_NODE)
  add_test(NAME local_090_float_diff_quick
    COMMAND ${FARMC_NODE} "${FARMC_SCRIPTS}/float_diff.mjs" --count 20000)
  set_tests_properties(local_090_float_diff_quick PROPERTIES LABELS "local")
endif()
