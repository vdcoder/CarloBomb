# End-to-end check of `carlobomb fixed`, the Bomb-backed FixedSolver.  Run by
# ctest:
#   cmake -DCLI=<carlobomb> -DLOOKUP=<fixed_bomb_lookup> -DWORK=<dir> -P fixed_smoke.cmake
#
# Builds the 16-bit Bomb with the constructor (enumeration up to 4 nodes, a
# second or two), then asks it questions.

function(run_program program expect_code expect_text)
  execute_process(COMMAND ${program} ${ARGN}
                  RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT code EQUAL expect_code)
    message(FATAL_ERROR "${program} ${ARGN}: exit ${code}, expected ${expect_code}\n${out}${err}")
  endif()
  if(NOT "${out}${err}" MATCHES "${expect_text}")
    message(FATAL_ERROR "${program} ${ARGN}: output lacks '${expect_text}'\n${out}${err}")
  endif()
endfunction()

function(run expect_code expect_text)
  run_program(${CLI} ${expect_code} "${expect_text}" ${ARGN})
endfunction()

set(W ${WORK}/fixed_smoke)
file(REMOVE_RECURSE ${W})
file(MAKE_DIRECTORY ${W})
set(B ${W}/fixed-run-16.frb)

run(0 "COMPLETE" bomb build --bits 16 --work-dir ${W}/w16 --enumerate-max 4 --workers 2 --progress 0)
run(0 "COMPLETE" bomb finalize --work-dir ${W}/w16 --output ${B})

# The suggested command of the brief: prefix, minimum, extension, graph, verification.
run(0 "Bomb: .*FixedRunBomb\\(16\\), complete: 65,536 certified minima.*Target: 01001000 \\(8 bits\\).*Result: MINIMAL.*no search.*Nodes: 3.*Extension: 0100100010100100 \\(entry 18596\\).*Node   Color   Link0   Link1.*Verification: PASS"
    fixed --bomb ${B} --target 01001000)
# All 16 bits: the entry itself.  One bit: one node.
run(0 "Nodes: 6.*Extension: 0100100011010110 \\(entry 18646\\): the target itself.*Verification: PASS"
    fixed --bomb ${B} --target 0100100011010110)
run(0 "Target: 1 \\(1 bit\\).*Nodes: 1.*Extension: 1111111111111111.*Verification: PASS" fixed --bomb ${B} --target 1)
run(0 "Nodes: 7.*Verification: PASS" fixed --bomb ${B} --target 0000000000001)

# A node budget below the minimum is refuted, at the minimum it is met.
run(1 "PROVEN_UNSAT: no REV with at most 2 nodes emits the target.*certified minimum is 3 nodes"
    fixed --bomb ${B} --target 01001000 --max-nodes 2)
run(0 "Result: MINIMAL.*Nodes: 3" fixed --bomb ${B} --target 01001000 --max-nodes 3)
run(1 "PROVEN_UNSAT" level --target 01001000 --nodes 2)   # the search says the same
run(0 "Nodes: 3" solve --target 01001000)

# The graph is an ordinary REV file.
run(0 "Graph written" fixed --bomb ${B} --target 01001000 --out ${W}/g.rev)
run(0 "Nodes: 3.*Verification: PASS" verify --target 01001000 --graph ${W}/g.rev)

# Every prefix of a target, and every length at once.
run(0 "Minimum of every prefix.*     1   0 +1   0000000000000000.*     9   010010001 +3 .*    15   010010001101011 +6   0100100011010110.*    16   0100100011010110 +6"
    fixed --bomb ${B} --target 0100100011010110 --profile)
run(0 "    10     1,024 +2 +6 +64 +562 +388 +2 +6   4.303.*    13     8,192 +2 +6 +64 +910 +5,836 +1,372 +2 +7.*    16    65,536 +2 +6 +64 +962 +14,348 +48,956 +1,198 +7   5.767.*   all   131,070 +32 +84 +688 +6,506 +46,402 +76,050 +1,308 +7.*    13 +7 +2   0000000000001.*nothing was searched"
    fixed --bomb ${B} --stats)

# Longer than the Bomb covers: no answer, a bound, and where to get the answer.
run(3 "UNSUPPORTED: this Bomb answers targets of 1 to 16 bits.*Nothing is claimed.*no REV with fewer than 5 nodes.*carlobomb solve --target 010010001000010000010000001"
    fixed --bomb ${B} --target 010010001000010000010000001)
run(3 "UNSUPPORTED" fixed --bomb ${B} --target 00000000000000001 --max-nodes 1)

# The lookup program has no search solver linked into it at all.
run_program(${LOOKUP} 0 "131070 strings of 1 to 16 bits against a direct scan of the table: all agree" ${B} --check)

# A smaller Bomb answers what it covers and no more.
run(0 "COMPLETE" bomb build --bits 10 --work-dir ${W}/w10 --enumerate-max 3 --progress 0)
run(0 "COMPLETE" bomb finalize --work-dir ${W}/w10 --output ${W}/b10.frb)
run(0 "FixedRunBomb\\(10\\).*Nodes: 3.*Extension: 0100100010 \\(entry 290\\)" fixed --bomb ${W}/b10.frb --target 01001000)
run(3 "UNSUPPORTED: this Bomb answers targets of 1 to 10 bits" fixed --bomb ${W}/b10.frb --target 01001000110)
run(0 "    10     1,024 +2 +6 +64 +562 +388 +2 +6" fixed --bomb ${W}/b10.frb --stats)

# Bombs that are refused.
run(3 "NOT COMPLETE" bomb build --bits 10 --work-dir ${W}/p10 --enumerate-max 3 --limit-jobs 20 --progress 0)
run(3 "PARTIAL" bomb finalize --work-dir ${W}/p10 --output ${W}/partial.frb --partial)
run(3 "Refused: this Bomb is not completely certified \\([0-9]+ of 1024 entries" fixed --bomb ${W}/partial.frb --target 0101)
run(3 "Refused: this Bomb is not completely certified" fixed --bomb ${W}/partial.frb --stats)
run(1 "Refused: not a FixedRunBomb file" fixed --bomb ${W}/w16/config.txt --target 0101)
run(2 "cannot read" fixed --bomb ${W}/nothing.frb --target 0101)
run_program(${LOOKUP} 1 "cannot use" ${W}/partial.frb --check)
run_program(${LOOKUP} 1 "cannot use" ${W}/nothing.frb)
run_program(${LOOKUP} 2 "usage" ${B} --frob)

# Questions that are not questions.
run(2 "invalid target" fixed --bomb ${B} --target 0120)
run(2 "fixed needs --bomb" fixed --target 0101)
run(2 "fixed needs --target" fixed --bomb ${B})
run(2 "bad value for --max-nodes" fixed --bomb ${B} --target 0101 --max-nodes 0)
run(2 "unknown option" fixed --bomb ${B} --target 0101 --frob 1)
run(0 "carlobomb fixed --bomb FILE --target S" fixed --help)

file(REMOVE_RECURSE ${W})
