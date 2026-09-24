cmake_minimum_required(VERSION 3.16)
project(farmos LANGUAGES CXX C VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(farmc
  src/main.cpp
  src/lexer.cpp
  src/parser.cpp
  src/sema.cpp
  src/emit_c.cpp
)
target_include_directories(farmc PRIVATE src runtime)

# Copy runtime next to farmc for default_runtime_dir discovery
add_custom_command(TARGET farmc POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:farmc>/runtime
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/runtime/farm_rt.c
    ${CMAKE_SOURCE_DIR}/runtime/farm_rt.h
    $<TARGET_FILE_DIR:farmc>/runtime/
)

enable_testing()

# Conformance harness script
set(FARMC_TESTS_DIR "${CMAKE_SOURCE_DIR}/spec/tests/M1")
if(WIN32)
  set(HARRIS "${CMAKE_SOURCE_DIR}/scripts/run_m1_tests.ps1")
  add_test(NAME m1_conformance
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${HARRIS}"
            -Farmc $<TARGET_FILE:farmc>
            -TestsDir "${FARMC_TESTS_DIR}"
  )
else()
  add_test(NAME m1_conformance
    COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/run_m1_tests.sh"
            $<TARGET_FILE:farmc> "${FARMC_TESTS_DIR}"
  )
endif()

# Also register each fixture as individual ctest for easier debugging
file(GLOB M1_EXPECTED "${FARMC_TESTS_DIR}/*.expected")
foreach(exp ${M1_EXPECTED})
  get_filename_component(name ${exp} NAME_WE)
  if(WIN32)
    add_test(NAME m1_${name}
      COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/run_one_m1.ps1"
              -Farmc $<TARGET_FILE:farmc>
              -TestsDir "${FARMC_TESTS_DIR}"
              -Name "${name}"
    )
  else()
    add_test(NAME m1_${name}
      COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/run_one_m1.sh"
              $<TARGET_FILE:farmc> "${FARMC_TESTS_DIR}" "${name}"
    )
  endif()
endforeach()
