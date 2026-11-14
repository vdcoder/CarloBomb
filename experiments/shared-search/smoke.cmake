# The shared search against tables the ordinary construction built.
#   cmake -DCLI=<carlobomb> -DSHARED=<shared_search> -DWORK=<dir> -P smoke.cmake
function(run_ok)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "${ARGN}: exit ${code}\n${out}${err}")
  endif()
  set(last_output "${out}" PARENT_SCOPE)
endfunction()
function(expect text)
  if(NOT last_output MATCHES "${text}")
    message(FATAL_ERROR "output lacks '${text}'\n${last_output}")
  endif()
endfunction()

set(W ${WORK}/shared_search_test)
file(REMOVE_RECURSE ${W})
file(MAKE_DIRECTORY ${W})

# A 12-bit Bomb by search alone, from one node up: every REV in it is the first
# the LevelSolver finds at the string's minimum, which is what the shared search
# must find too.  All 2,048 strings that start with 0: the same node count, the
# same REV.
run_ok(${CLI} bomb build --bits 12 --work-dir ${W}/d12 --enumerate-max 0 --workers 2 --progress 0 --quiet)
run_ok(${CLI} bomb finalize --work-dir ${W}/d12 --output ${W}/d12.frb)
run_ok(${SHARED} --bits 12 --check ${W}/d12.frb)
expect("2,048 strings found by search, 0 without a REV")
expect("the node count of the table for 2,048, another for 0")
expect("link for link and color for color, for 2,048 ")
# The same in 16 classes and 3 processes: the outcome does not depend on either.
run_ok(${SHARED} --bits 12 --prefix-bits 5 --jobs 3 --check ${W}/d12.frb)
expect("in 16 classes of 16")
expect("the node count of the table for 2,048, another for 0")
expect("link for link and color for color, for 2,048 ")

# A level as the ladder builds it: on the Bomb one bit shorter.  The table the
# ladder wrote, entry for entry.
run_ok(${CLI} bomb ladder --dir ${W}/lad --from ${W}/d12.frb --to 13 --workers 2 --progress 0 --quiet)
run_ok(${SHARED} --bits 13 --prefix-bits 4 --jobs 2 --base ${W}/lad/fixed-run-12.frb --check ${W}/lad/fixed-run-13.frb)
expect("2,048 settled by the REV of the base, 0 without a REV")
expect("the node count of the table for 4,096, another for 0")
expect("link for link and color for color, for 4,096 ")
