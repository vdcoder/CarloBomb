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
add_library(carlobomb_fixed_run STATIC bombs/fixed-run/fixed_run.c bombs/fixed-run/enumerate.c
  bombs/fixed-run/compact.c)
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
# A table of full size without the search: files of many gigabytes written,
# addressed and read (benchmarks/FIXED_RUN_31_FEASIBILITY.md).
add_executable(big_table benchmarks/big_table.c)
target_link_libraries(big_table PRIVATE carlobomb_fixed_run)
# The fixed-width and the compact file of one Bomb through the solver that
# reads both: sizes, opening, lookups, memory.
add_executable(compact_bench benchmarks/compact_bench.c)
target_link_libraries(compact_bench PRIVATE carlobomb_fixed_bomb)
if(UNIX)
  target_link_libraries(compact_bench PRIVATE m)
endif()

add_executable(fixed_bomb_longer benchmarks/fixed_bomb_longer.c)
target_link_libraries(fixed_bomb_longer PRIVATE carlobomb_fixed_bomb carlobomb_minimal m)

# What a Bomb beyond 16 bits would cost: samples the ladder construction.
if(UNIX)
  add_executable(ladder_pilot benchmarks/ladder_pilot.c)
  target_link_libraries(ladder_pilot PRIVATE carlobomb_minimal m)
endif()

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
  # It times a busy loop against the task clock: not next to tests that keep
  # every processor busy with worker processes.
  set_tests_properties(perf_counter_plumbing PROPERTIES RUN_SERIAL TRUE)
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

# FixedRunBomb format version 2 (up to 24 bits) and the ladder construction:
# every level of small ladders against the MinimalSolver, string by string.
add_executable(test_fixed_run_ladder bombs/fixed-run/tests/test_fixed_run_ladder.c)
target_include_directories(test_fixed_run_ladder PRIVATE tests)
target_link_libraries(test_fixed_run_ladder PRIVATE carlobomb_fixed_run carlobomb_minimal)
if(CARLOBOMB_HAVE_BOMB_BUILD)
  target_link_libraries(test_fixed_run_ladder PRIVATE carlobomb_fixed_run_build)
endif()
add_test(NAME test_fixed_run_ladder
  COMMAND test_fixed_run_ladder ${CMAKE_CURRENT_BINARY_DIR}/fixed_run_ladder_test_work)
set_tests_properties(test_fixed_run_ladder PROPERTIES TIMEOUT 600)
# The compact format (version 4): packing, layout, conversion both ways, the
# prefix index against a scan of the table, and damaged files.
add_executable(test_compact bombs/fixed-run/tests/test_compact.c)
target_include_directories(test_compact PRIVATE tests)
target_link_libraries(test_compact PRIVATE carlobomb_fixed_run carlobomb_minimal)
add_test(NAME test_compact COMMAND test_compact ${CMAKE_CURRENT_BINARY_DIR}/compact_test)
# compact.c compiled into the test, to change the bytes of an opened Bomb.
add_executable(test_compact_internal bombs/fixed-run/tests/test_compact_internal.c)
target_include_directories(test_compact_internal PRIVATE tests)
target_link_libraries(test_compact_internal PRIVATE carlobomb_fixed_run carlobomb_minimal)
add_test(NAME test_compact_internal COMMAND test_compact_internal ${CMAKE_CURRENT_BINARY_DIR}/compact_internal)
add_test(NAME test_compact_unmapped COMMAND test_compact ${CMAKE_CURRENT_BINARY_DIR}/compact_test_unmapped)
set_tests_properties(test_compact_unmapped PROPERTIES ENVIRONMENT "CARLOBOMB_NO_MMAP=1")

# The same with tables read into memory instead of mapped from their files.
add_test(NAME test_fixed_run_ladder_unmapped
  COMMAND test_fixed_run_ladder ${CMAKE_CURRENT_BINARY_DIR}/fixed_run_ladder_unmapped_work)
set_tests_properties(test_fixed_run_ladder_unmapped PROPERTIES TIMEOUT 600
  ENVIRONMENT "CARLOBOMB_NO_MMAP=1")

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
  if(UNIX)
    target_link_libraries(ladder_pilot PRIVATE carlobomb_level_amd64)
  endif()
endif()
set(CARLOBOMB_FIXED_CROSS_BITS 16)
if(CARLOBOMB_SANITIZE AND NOT CARLOBOMB_HAVE_AMD64_ASM)
  set(CARLOBOMB_FIXED_CROSS_BITS 12)
endif()
add_test(NAME test_fixed_bomb_solver
  COMMAND test_fixed_bomb_solver ${CMAKE_CURRENT_BINARY_DIR}/fixed_bomb_test_work
          --cross-bits ${CARLOBOMB_FIXED_CROSS_BITS})
set_tests_properties(test_fixed_bomb_solver PROPERTIES TIMEOUT 900)
# The solver and the compact reader compiled into the test, to change the
# bytes of an opened compact Bomb.
add_executable(test_fixed_bomb_internal solvers/fixed/bomb/tests/test_fixed_bomb_internal.c)
target_include_directories(test_fixed_bomb_internal PRIVATE tests solvers/fixed/bomb bombs/fixed-run)
target_link_libraries(test_fixed_bomb_internal PRIVATE carlobomb_fixed_run carlobomb_minimal)
add_test(NAME test_fixed_bomb_internal
  COMMAND test_fixed_bomb_internal ${CMAKE_CURRENT_BINARY_DIR}/fixed_bomb_internal)

if(CARLOBOMB_HAVE_BOMB_BUILD)
  # The bomb commands end to end.  bomb_16 builds the whole 16-bit table with
  # enumeration up to 4 nodes; bomb_16full builds it with the default
  # settings (enumeration up to 5, 312 million graph runs) and is left out
  # of sanitizer builds, where that takes minutes.
  set(CARLOBOMB_BOMB_MODES small 16)
  if(NOT CARLOBOMB_SANITIZE)
    list(APPEND CARLOBOMB_BOMB_MODES 16full)
  endif()
  # A compact file that names a source it does not hold, for bomb_small.
  add_executable(compact_fixture tests/compact_fixture.c)
  target_link_libraries(compact_fixture PRIVATE carlobomb_fixed_run)
  foreach(mode ${CARLOBOMB_BOMB_MODES})
    add_test(NAME bomb_${mode}
      COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:carlobomb> -DWORK=${CMAKE_CURRENT_BINARY_DIR}
              -DHAVE_ASM=${CARLOBOMB_HAVE_AMD64_ASM} -DMODE=${mode} -DFIXTURE=$<TARGET_FILE:compact_fixture>
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

# The same check of size that is run by hand on 26 to 28 bits, on a table
# small enough for every run of the tests.
add_test(NAME big_table_small COMMAND big_table --bits 18 --out ${CMAKE_CURRENT_BINARY_DIR}/big_table_18.frb)
set_tests_properties(big_table_small PROPERTIES PASS_REGULAR_EXPRESSION "PASS")

add_test(NAME legacy_original_runs COMMAND legacy_original)
set_tests_properties(legacy_original_runs PROPERTIES
  PASS_REGULAR_EXPRESSION "SOLUTION \\(3 nodes\\).*0: B -> \\[2, 1\\].*1: W -> \\[0, 0\\].*2: W -> \\[1, 2\\]")

add_test(NAME cli_smoke
  COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:carlobomb> -DWORK=${CMAKE_CURRENT_BINARY_DIR}
          -DHAVE_ASM=${CARLOBOMB_HAVE_AMD64_ASM}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.cmake)

# ---- experiments -----------------------------------------------------------
# Measurements for a design decision; no format, solver or Bomb depends on
# them.  GCC or Clang on x86-64 or any 64-bit target with __int128.
option(CARLOBOMB_EXPERIMENTS "Build the experiments under experiments/" ON)
if(CARLOBOMB_EXPERIMENTS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  # Colorless REVs (PR 8): canonical topologies, recoloring, topology codes.
  add_library(carlobomb_colorless STATIC experiments/colorless/colorless.c)
  target_include_directories(carlobomb_colorless PUBLIC experiments/colorless)
  target_link_libraries(carlobomb_colorless PUBLIC carlobomb_rev)
  add_executable(test_colorless experiments/colorless/test_colorless.c)
  target_include_directories(test_colorless PRIVATE tests)
  target_link_libraries(test_colorless PRIVATE carlobomb_colorless carlobomb_fixed_run carlobomb_level)
  add_test(NAME test_colorless COMMAND test_colorless)
  # Every REV of a complete Bomb as a topology and a coloring.
  add_executable(colorless_scan experiments/colorless/colorless_scan.c)
  target_include_directories(colorless_scan PRIVATE benchmarks)
  target_link_libraries(colorless_scan PRIVATE carlobomb_colorless carlobomb_fixed_run m)
  # What recoloring costs, REV by REV, against verification of the colored REV.
  add_executable(colorless_bench experiments/colorless/colorless_bench.c)
  target_include_directories(colorless_bench PRIVATE benchmarks)
  target_link_libraries(colorless_bench PRIVATE carlobomb_colorless carlobomb_fixed_run m)
  # Candidate layouts built in memory and measured end to end.
  add_executable(colorless_lookup experiments/colorless/colorless_lookup.c)
  target_include_directories(colorless_lookup PRIVATE benchmarks)
  target_link_libraries(colorless_lookup PRIVATE carlobomb_colorless carlobomb_fixed_run m)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    # counting node counts in a word needs the population count instruction
    target_compile_options(colorless_lookup PRIVATE -mpopcnt)
  endif()
  if(UNIX)
    # Lookups with the files out of the page cache.
    add_executable(colorless_cold experiments/colorless/colorless_cold.c)
    target_include_directories(colorless_cold PRIVATE benchmarks)
    target_link_libraries(colorless_cold PRIVATE carlobomb_fixed_run)
  endif()
  if(CARLOBOMB_HAVE_BOMB_BUILD)
    # The tools on a 13-bit Bomb built for the occasion.
    add_test(NAME colorless_smoke
      COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:carlobomb> -DSCAN=$<TARGET_FILE:colorless_scan>
              -DLOOKUP=$<TARGET_FILE:colorless_lookup> -DBENCH=$<TARGET_FILE:colorless_bench>
              -DWORK=${CMAKE_CURRENT_BINARY_DIR}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/experiments/colorless/smoke.cmake)
    set_tests_properties(colorless_smoke PROPERTIES TIMEOUT 600)
  endif()
endif()
