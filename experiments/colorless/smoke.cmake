# The colorless-REV experiment tools end to end, on a small Bomb built for
# the occasion.  Run by ctest:
#   cmake -DCLI=<carlobomb> -DSCAN=<colorless_scan> -DLOOKUP=<colorless_lookup>
#         -DBENCH=<colorless_bench> -DWORK=<dir> -P smoke.cmake

function(run expect_text)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "${ARGN}: exit ${code}\n${out}${err}")
  endif()
  if(NOT "${out}${err}" MATCHES "${expect_text}")
    message(FATAL_ERROR "${ARGN}: output lacks '${expect_text}'\n${out}${err}")
  endif()
endfunction()

set(W ${WORK}/colorless_smoke)
file(REMOVE_RECURSE ${W})
file(MAKE_DIRECTORY ${W})

# A complete 13-bit Bomb: 8,192 strings, REVs of up to 7 nodes, two of them
# found by the search on the complete 12-bit Bomb below.
run("^$" ${CLI} bomb build --bits 10 --work-dir ${W}/w --enumerate-max 3 --workers 2 --quiet)
run("COMPLETE" ${CLI} bomb finalize --work-dir ${W}/w --output ${W}/b10.frb)
run("The ladder is complete" ${CLI} bomb ladder --dir ${W}/lad --from ${W}/b10.frb --to 13 --workers 2 --progress 0)
file(SHA256 ${W}/lad/fixed-run-13.frb before)

# Every REV: reachable, restored from its canonical form where the search
# numbered it, the search for colors against all 2^N colorings.
run("Canonical renumbering changed the first 256 output bits of 0 REVs.*      7 +2 +2 +1\\.00.*      6 +[0-9,]+ +[0-9,]+ \\([0-9.]+%\\) +[0-9,]+ +[0-9,]+ +[0-9]+ +0 +0 +0\n"
    ${SCAN} --bomb ${W}/lad/fixed-run-13.frb --brute --data ${W}/scan.data)
file(READ ${W}/scan.data data)
if(NOT data MATCHES "bits 13\n.*class 7 strings 2 restored 2 solver_numbered 2\n")
  message(FATAL_ERROR "scan data:\n${data}")
endif()

# Every candidate layout gives back every stored REV.
run("V 0 wrong, W 0, Y 0, X 0; X without renumbering: 0 that do not emit.*: 0 differ.*PASS: every layout gives the stored REVs"
    ${LOOKUP} --bomb ${W}/lad/fixed-run-13.frb --queries 20000)
run("recolor " ${BENCH} --bomb ${W}/lad/fixed-run-13.frb --sample 500 --hard 20 --passes 3)

# Nothing of this writes to the Bomb.
file(SHA256 ${W}/lad/fixed-run-13.frb after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "the Bomb changed")
endif()
file(REMOVE_RECURSE ${W})
