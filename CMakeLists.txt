cmake_minimum_required(VERSION 3.16)
project(carlobomb C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CARLOBOMB_SANITIZE "Build with AddressSanitizer and UBSan (GCC/Clang)" OFF)
option(CARLOBOMB_WERROR "Treat warnings as errors" OFF)

if(MSVC)
  add_compile_options(/W4)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  if(CARLOBOMB_WERROR)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
  # clock_gettime with -std=c11
  add_compile_definitions(_POSIX_C_SOURCE=200809L)
  if(CARLOBOMB_WERROR)
    add_compile_options(-Werror)
  endif()
  if(CARLOBOMB_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

# Recorded in benchmark output.  Evaluated at configure time: re-run cmake
# after committing to refresh it.
set(CARLOBOMB_REVISION "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --dirty
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  OUTPUT_VARIABLE CARLOBOMB_GIT_OUT OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET RESULT_VARIABLE CARLOBOMB_GIT_RC)
  if(CARLOBOMB_GIT_RC EQUAL 0)
    set(CARLOBOMB_REVISION "${CARLOBOMB_GIT_OUT}")
  endif()
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" CARLOBOMB_BT)
string(STRIP "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${CARLOBOMB_BT}}" CARLOBOMB_FLAGS)
if(CARLOBOMB_SANITIZE)
  string(APPEND CARLOBOMB_FLAGS " -fsanitize=address,undefined")
endif()

# ---- libraries -----------------------------------------------------------

add_library(carlobomb_rev STATIC core/rev/rev.c)
target_include_directories(carlobomb_rev PUBLIC core/rev)

add_library(carlobomb_level STATIC solvers/level/fastest/c/level_solver.c)
target_include_directories(carlobomb_level PUBLIC solvers/level/fastest/c)
target_link_libraries(carlobomb_level PUBLIC carlobomb_rev)

add_library(carlobomb_minimal STATIC solvers/minimal/minimal_solver.c)
target_include_directories(carlobomb_minimal PUBLIC solvers/minimal)
target_link_libraries(carlobomb_minimal PUBLIC carlobomb_level)

# AMD64 assembly challenger: Linux/ELF x86-64 with the System V ABI only.
# Everywhere else it is not built and the portable C solver is all there is.
option(CARLOBOMB_AMD64_ASM "Build the experimental AMD64 assembly LevelSolver" ON)
set(CARLOBOMB_HAVE_AMD64_ASM OFF)
if(CARLOBOMB_AMD64_ASM AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$"
   AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT MSVC)
  enable_language(ASM)
  set(CARLOBOMB_HAVE_AMD64_ASM ON)
  set(CARLOBOMB_ASM_DIR solvers/level/experimental/amd64/asm)
  add_library(carlobomb_level_amd64 STATIC
    ${CARLOBOMB_ASM_DIR}/asm_solver.c
    ${CARLOBOMB_ASM_DIR}/search_sysv.S)
  target_include_directories(carlobomb_level_amd64 PUBLIC ${CARLOBOMB_ASM_DIR})
  target_link_libraries(carlobomb_level_amd64 PUBLIC carlobomb_level)
  target_compile_definitions(carlobomb_level_amd64 INTERFACE CARLOBOMB_HAVE_AMD64_ASM=1)
endif()
message(STATUS "CarloBomb AMD64 assembly solver: ${CARLOBOMB_HAVE_AMD64_ASM}")

# ---- executables ---------------------------------------------------------

# The historical solver, built exactly as supplied.  Its own warnings are
# left alone rather than fixed in the source.
add_executable(legacy_original solvers/level/fastest/c/legacy_original.c)
if(NOT MSVC)
  target_compile_options(legacy_original PRIVATE -Wno-unused-parameter -Wno-conversion
                         -Wno-sign-conversion -Wno-unused-value)
endif()

add_executable(carlobomb tools/carlobomb.c)
target_include_directories(carlobomb PRIVATE benchmarks)
target_link_libraries(carlobomb PRIVATE carlobomb_minimal)

add_executable(baseline benchmarks/baseline.c)
target_include_directories(baseline PRIVATE tests)
target_link_libraries(baseline PRIVATE carlobomb_level)
target_compile_definitions(baseline PRIVATE
  CARLOBOMB_REVISION="${CARLOBOMB_REVISION}"
  CARLOBOMB_C_FLAGS="${CARLOBOMB_FLAGS}"
  CARLOBOMB_SYSTEM="${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")

# ---- tests ---------------------------------------------------------------

enable_testing()

foreach(t test_rev test_level_solver test_minimal_solver test_oracle test_legacy_equivalence)
  add_executable(${t} tests/${t}.c)
  target_link_libraries(${t} PRIVATE carlobomb_minimal)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

if(NOT MSVC)
  # Code included from the unmodified legacy source.
  foreach(t test_legacy_equivalence baseline)
    target_compile_options(${t} PRIVATE -Wno-conversion -Wno-sign-conversion)
  endforeach()
endif()

if(CARLOBOMB_HAVE_AMD64_ASM)
  target_link_libraries(carlobomb PRIVATE carlobomb_level_amd64)

  add_executable(test_asm_equivalence tests/test_asm_equivalence.c tests/abi_check_sysv.S)
  target_link_libraries(test_asm_equivalence PRIVATE carlobomb_minimal carlobomb_level_amd64)
  add_test(NAME test_asm_equivalence COMMAND test_asm_equivalence)

  add_executable(test_oracle_amd64 tests/test_oracle.c)
  target_compile_definitions(test_oracle_amd64 PRIVATE CARLOBOMB_ORACLE_AMD64=1)
  target_link_libraries(test_oracle_amd64 PRIVATE carlobomb_minimal carlobomb_level_amd64)
  add_test(NAME test_oracle_amd64 COMMAND test_oracle_amd64)

  add_executable(asm_comparison benchmarks/asm_comparison.c)
  target_link_libraries(asm_comparison PRIVATE carlobomb_level_amd64 m)
  target_compile_definitions(asm_comparison PRIVATE
    CARLOBOMB_REVISION="${CARLOBOMB_REVISION}"
    CARLOBOMB_C_FLAGS="${CARLOBOMB_FLAGS}"
    CARLOBOMB_SYSTEM="${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
endif()

add_test(NAME legacy_original_runs COMMAND legacy_original)
set_tests_properties(legacy_original_runs PROPERTIES
  PASS_REGULAR_EXPRESSION "SOLUTION \\(3 nodes\\).*0: B -> \\[2, 1\\].*1: W -> \\[0, 0\\].*2: W -> \\[1, 2\\]")

add_test(NAME cli_smoke
  COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:carlobomb> -DWORK=${CMAKE_CURRENT_BINARY_DIR}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.cmake)
