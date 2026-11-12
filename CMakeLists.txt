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

# FixedRunBomb.  The file format and the enumerator are plain C11; building a
# table (journal, worker processes) needs POSIX.
add_library(carlobomb_fixed_run STATIC bombs/fixed-run/fixed_run.c bombs/fixed-run/enumerate.c)
target_include_directories(carlobomb_fixed_run PUBLIC bombs/fixed-run)
target_link_libraries(carlobomb_fixed_run PUBLIC carlobomb_rev)
set(CARLOBOMB_HAVE_BOMB_BUILD OFF)
if(UNIX)
  set(CARLOBOMB_HAVE_BOMB_BUILD ON)
  add_library(carlobomb_fixed_run_build STATIC
    bombs/fixed-run/journal.c bombs/fixed-run/construct.c bombs/fixed-run/statistics.c)
  target_link_libraries(carlobomb_fixed_run_build PUBLIC carlobomb_fixed_run carlobomb_minimal)
  target_compile_definitions(carlobomb_fixed_run_build INTERFACE CARLOBOMB_HAVE_BOMB_BUILD=1)
endif()

# Bomb-backed FixedSolver: minimum-node REVs for strings of up to 16 bits by
# lookup in a FixedRunBomb.  It links the format code and the REV core and
# nothing else: no search solver is part of it.
add_library(carlobomb_fixed_bomb STATIC solvers/fixed/bomb/fixed_bomb_solver.c)
target_include_directories(carlobomb_fixed_bomb PUBLIC solvers/fixed/bomb)
target_link_libraries(carlobomb_fixed_bomb PUBLIC carlobomb_fixed_run)

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
  set(CARLOBOMB_REPR_DIR solvers/level/experimental/amd64/repr)
  add_library(carlobomb_level_amd64 STATIC
    ${CARLOBOMB_ASM_DIR}/asm_solver.c
    ${CARLOBOMB_ASM_DIR}/search_sysv.S
    ${CARLOBOMB_REPR_DIR}/k_b4.S
    ${CARLOBOMB_REPR_DIR}/k_hyb.S
    ${CARLOBOMB_REPR_DIR}/k_n8g.S
    ${CARLOBOMB_REPR_DIR}/k_n8g_bmi2.S
    ${CARLOBOMB_REPR_DIR}/k_p16m.S
    ${CARLOBOMB_REPR_DIR}/k_p16g.S
    ${CARLOBOMB_REPR_DIR}/k_p16x.S
    ${CARLOBOMB_REPR_DIR}/k_hyb_f8.S
    ${CARLOBOMB_REPR_DIR}/k_n8g_f8.S
    ${CARLOBOMB_REPR_DIR}/k_n8s.S
    ${CARLOBOMB_REPR_DIR}/k_n8s_bmi2.S
    ${CARLOBOMB_REPR_DIR}/k_n8x.S
    ${CARLOBOMB_REPR_DIR}/k_hybx.S
    ${CARLOBOMB_REPR_DIR}/k_n8x_count.S
    ${CARLOBOMB_REPR_DIR}/k_hybx_count.S)
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

add_executable(carlobomb tools/carlobomb.c tools/fixed_run_cli.c tools/fixed_bomb_cli.c)
target_include_directories(carlobomb PRIVATE benchmarks)
target_link_libraries(carlobomb PRIVATE carlobomb_minimal carlobomb_fixed_run carlobomb_fixed_bomb)
if(CARLOBOMB_HAVE_BOMB_BUILD)
  target_link_libraries(carlobomb PRIVATE carlobomb_fixed_run_build)
endif()

# Lookup solver alone: its link line is the proof that it runs no search.
add_executable(fixed_bomb_lookup benchmarks/fixed_bomb_lookup.c)
target_link_libraries(fixed_bomb_lookup PRIVATE carlobomb_fixed_bomb)

# What the Bomb knows about targets longer than it covers, against the search.
add_executable(fixed_bomb_longer benchmarks/fixed_bomb_longer.c)
target_link_libraries(fixed_bomb_longer PRIVATE carlobomb_fixed_bomb carlobomb_minimal m)

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
  # A wrong search kernel can loop forever; do not let it stall the suite.
  set_tests_properties(test_asm_equivalence test_oracle_amd64 PROPERTIES TIMEOUT 300)

  add_executable(asm_comparison benchmarks/asm_comparison.c)
  target_link_libraries(asm_comparison PRIVATE carlobomb_level_amd64 m)
  target_compile_definitions(asm_comparison PRIVATE
    CARLOBOMB_REVISION="${CARLOBOMB_REVISION}"
    CARLOBOMB_C_FLAGS="${CARLOBOMB_FLAGS}"
    CARLOBOMB_SYSTEM="${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")

  add_executable(repr_comparison benchmarks/repr_comparison.c)
  target_link_libraries(repr_comparison PRIVATE carlobomb_level_amd64 m)
  target_compile_definitions(repr_comparison PRIVATE CARLOBOMB_REVISION="${CARLOBOMB_REVISION}")

  add_executable(characterize benchmarks/characterize.c)
  target_link_libraries(characterize PRIVATE carlobomb_level_amd64 m)
  target_compile_definitions(characterize PRIVATE CARLOBOMB_REVISION="${CARLOBOMB_REVISION}")
  add_test(NAME perf_counter_plumbing COMMAND characterize --perf-selftest)
endif()

# FixedRunBomb: format, enumerator, construction, interruption, damage.
add_executable(test_fixed_run bombs/fixed-run/tests/test_fixed_run.c)
target_include_directories(test_fixed_run PRIVATE tests)
target_link_libraries(test_fixed_run PRIVATE carlobomb_fixed_run carlobomb_minimal)
if(CARLOBOMB_HAVE_BOMB_BUILD)
  target_link_libraries(test_fixed_run PRIVATE carlobomb_fixed_run_build)
endif()
add_test(NAME test_fixed_run
  COMMAND test_fixed_run ${CMAKE_CURRENT_BINARY_DIR}/fixed_run_test_work)
set_tests_properties(test_fixed_run PROPERTIES TIMEOUT 600)

# Bomb-backed FixedSolver: every string of 1 to 16 bits against a direct scan
# of the table and against the MinimalSolver.  The comparison with the search
# is exhaustive; without the assembly engine and under sanitizers that would
# take many minutes, and is then limited to strings of up to 12 bits.
add_executable(test_fixed_bomb_solver solvers/fixed/bomb/tests/test_fixed_bomb_solver.c)
target_include_directories(test_fixed_bomb_solver PRIVATE tests)
target_link_libraries(test_fixed_bomb_solver PRIVATE carlobomb_fixed_bomb carlobomb_minimal)
if(CARLOBOMB_HAVE_BOMB_BUILD)
  target_link_libraries(test_fixed_bomb_solver PRIVATE carlobomb_fixed_run_build)
endif()
if(CARLOBOMB_HAVE_AMD64_ASM)
  target_link_libraries(test_fixed_bomb_solver PRIVATE carlobomb_level_amd64)
  target_link_libraries(fixed_bomb_longer PRIVATE carlobomb_level_amd64)
endif()
set(CARLOBOMB_FIXED_CROSS_BITS 16)
if(CARLOBOMB_SANITIZE AND NOT CARLOBOMB_HAVE_AMD64_ASM)
  set(CARLOBOMB_FIXED_CROSS_BITS 12)
endif()
add_test(NAME test_fixed_bomb_solver
  COMMAND test_fixed_bomb_solver ${CMAKE_CURRENT_BINARY_DIR}/fixed_bomb_test_work
          --cross-bits ${CARLOBOMB_FIXED_CROSS_BITS})
set_tests_properties(test_fixed_bomb_solver PROPERTIES TIMEOUT 900)

if(CARLOBOMB_HAVE_BOMB_BUILD)
  # The bomb commands end to end.  bomb_16 builds the whole 16-bit table with
  # enumeration up to 4 nodes; bomb_16full builds it with the default
  # settings (enumeration up to 5, 312 million graph runs) and is left out
  # of sanitizer builds, where that takes minutes.
  set(CARLOBOMB_BOMB_MODES small 16)
  if(NOT CARLOBOMB_SANITIZE)
    list(APPEND CARLOBOMB_BOMB_MODES 16full)
  endif()
  foreach(mode ${CARLOBOMB_BOMB_MODES})
    add_test(NAME bomb_${mode}
      COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:carlobomb> -DWORK=${CMAKE_CURRENT_BINARY_DIR}
              -DHAVE_ASM=${CARLOBOMB_HAVE_AMD64_ASM} -DMODE=${mode}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/bomb_smoke.cmake)
    set_tests_properties(bomb_${mode} PROPERTIES TIMEOUT 900)
  endforeach()
endif()

if(CARLOBOMB_HAVE_BOMB_BUILD)
  # `carlobomb fixed` end to end, on a 16-bit Bomb built for the occasion.
  add_test(NAME fixed_smoke
    COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:carlobomb> -DLOOKUP=$<TARGET_FILE:fixed_bomb_lookup>
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixed_smoke.cmake)
  set_tests_properties(fixed_smoke PROPERTIES TIMEOUT 900)
endif()

add_test(NAME legacy_original_runs COMMAND legacy_original)
set_tests_properties(legacy_original_runs PROPERTIES
  PASS_REGULAR_EXPRESSION "SOLUTION \\(3 nodes\\).*0: B -> \\[2, 1\\].*1: W -> \\[0, 0\\].*2: W -> \\[1, 2\\]")

add_test(NAME cli_smoke
  COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:carlobomb> -DWORK=${CMAKE_CURRENT_BINARY_DIR}
          -DHAVE_ASM=${CARLOBOMB_HAVE_AMD64_ASM}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.cmake)
