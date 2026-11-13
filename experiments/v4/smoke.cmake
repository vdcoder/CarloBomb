# The matrix of the v4 candidates end to end, on a small Bomb built for the
# occasion.  Run by ctest:
#   cmake -DCLI=<carlobomb> -DMATRIX=<v4_matrix> -DWORK=<dir> -P smoke.cmake

function(run expect_text)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "${ARGN}: exit ${code}\n${out}${err}")
  endif()
  if(NOT "${out}${err}" MATCHES "${expect_text}")
    message(FATAL_ERROR "${ARGN}: output lacks '${expect_text}'\n${out}${err}")
  endif()
  set(last_output "${out}" PARENT_SCOPE)
endfunction()

set(W ${WORK}/v4_smoke)
file(REMOVE_RECURSE ${W})
file(MAKE_DIRECTORY ${W}/scratch)

# A complete 13-bit Bomb and its compact file (PR 8), made the way the real ones are.
run("^$" ${CLI} bomb build --bits 10 --work-dir ${W}/w --enumerate-max 3 --workers 2 --quiet)
run("COMPLETE" ${CLI} bomb finalize --work-dir ${W}/w --output ${W}/b10.frb)
run("The ladder is complete" ${CLI} bomb ladder --dir ${W}/lad --from ${W}/b10.frb --to 13 --workers 2 --progress 0)
run("% of the source" ${CLI} bomb compact --file ${W}/lad/fixed-run-13.frb --output ${W}/c13.frb)
file(SHA256 ${W}/lad/fixed-run-13.frb before)

# The twelve combinations: each made, checked for every string of every
# length, and measured.  PASS means no file gave anything the Bomb does not.
run("PASS\n$" ${MATRIX} run --bomb ${W}/lad/fixed-run-13.frb --scratch ${W}/scratch --compact ${W}/c13.frb
    --variants all --queries 4000 --rounds 2 --threads 1,2 --cold 20)
foreach(variant "fields,colors,whole" "fields,colors,half" "base-n,colors,whole" "base-n,colors,half"
                "rank,colors,whole" "rank,colors,half" "fields,colorless,whole" "fields,colorless,half"
                "base-n,colorless,whole" "base-n,colorless,half" "rank,colorless,whole" "rank,colorless,half")
  # size, share, conversion time and memory, then: 0 REVs other than the stored one, 0 other next bits
  if(NOT last_output MATCHES "\n  ${variant} +[0-9,]+ +[0-9.]+% +[0-9.]+ +[0-9]+ +0 +0\n")
    message(FATAL_ERROR "no row for ${variant} with every stored REV returned:\n${last_output}")
  endif()
endforeach()
if(NOT last_output MATCHES "8,192 of 13 bits: 0 wrong node counts, 0 without a REV that emits them")
  message(FATAL_ERROR "the check of every string is missing:\n${last_output}")
endif()
# every shorter string: a fewest-nodes extension, the one the rule gives, and the comparison on demand
if(NOT last_output MATCHES "8,190 shorter strings: 0 not answered by an extension with the fewest nodes,\n +0 by another such extension than the rule gives, 0 with another answer")
  message(FATAL_ERROR "the check of the shorter strings is missing:\n${last_output}")
endif()
if(last_output MATCHES "shorter strings: [1-9]|\n +[1-9][0-9,]* by another such extension|, [1-9][0-9,]* with another answer")
  message(FATAL_ERROR "a shorter string was answered wrongly:\n${last_output}")
endif()
file(GLOB left ${W}/scratch/*)
if(left)
  message(FATAL_ERROR "the matrix left files behind: ${left}")
endif()

# Without --variants: the approved design alone, beside the fixed-width file.
run("\n  rank,colorless,half +[0-9,]+ +[0-9.]+% +[0-9.]+ +[0-9]+ +0 +0\n.*PASS\n$"
    ${MATRIX} run --bomb ${W}/lad/fixed-run-13.frb --scratch ${W}/scratch --queries 2000 --rounds 1 --threads 1 --cold 0)
if(last_output MATCHES "fields,colors|rank,colors,|whole")
  message(FATAL_ERROR "the default is not the approved design alone:\n${last_output}")
endif()

# How often the Bomb picks a next bit: a row for every length, and the total.
# Of the 8,191 strings of 0 to 12 bits each has two extensions that either differ or do not.
run("Bomb: FixedRunBomb\\(13\\).*\n  12 +4,096 +[0-9,]+ +[0-9.]+% +[0-9,]+ +[0-9,]+ +[0-9,]+ +[0-9,]+\n +8,191 "
    ${MATRIX} choices --bomb ${W}/lad/fixed-run-13.frb)

# A variant with its own spacing of the stored counts, and a name that is none.
run("\n  rank,colorless,half,c64 +[0-9,]+ +[0-9.]+% +[0-9.]+ +[0-9]+ +0 +0\n.*PASS\n$"
    ${MATRIX} run --bomb ${W}/lad/fixed-run-13.frb --scratch ${W}/scratch --variants "rank,colorless,half,c64"
    --queries 2000 --rounds 1 --threads 1 --cold 0)
execute_process(COMMAND ${MATRIX} run --bomb ${W}/lad/fixed-run-13.frb --scratch ${W}/scratch --variants "rank,plaid"
                RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(code EQUAL 0 OR NOT "${err}" MATCHES "no such variant: rank,plaid")
  message(FATAL_ERROR "an unknown variant was taken: exit ${code}\n${out}${err}")
endif()
file(GLOB left ${W}/scratch/*)
if(left)
  message(FATAL_ERROR "the matrix left files behind: ${left}")
endif()

# One file made and kept; a second conversion onto it is refused.
run("rank,colorless,half: [0-9,]+ bytes" ${MATRIX} convert --bomb ${W}/lad/fixed-run-13.frb
    --output ${W}/scratch/keep.cbx --variant rank,colorless,half)
execute_process(COMMAND ${MATRIX} convert --bomb ${W}/lad/fixed-run-13.frb --output ${W}/scratch/keep.cbx
                        --variant rank,colorless,half
                RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(code EQUAL 0 OR NOT "${err}" MATCHES "the output file exists")
  message(FATAL_ERROR "a file was replaced: exit ${code}\n${out}${err}")
endif()

# One string asked of that file: the comparison of its two extensions on demand, the extension the
# navigation bits lead to, the REV.  The node counts of a string do not depend on the Bomb they are read from.
run("Target: 0101010 \\(7 bits.*Nodes: 2, the certified minimum\nExtension: 0101010101010: .*Followed by 0: 4 nodes\\.  Followed by 1: 2 nodes\\.  1 needs fewer, by 2\\..*Verification: PASS"
    ${MATRIX} ask --file ${W}/scratch/keep.cbx --target 0101010)
run("Followed by 0: 4 nodes\\.  Followed by 1: 4 nodes\\.  Neither needs fewer\\..*Verification: PASS"
    ${MATRIX} ask --file ${W}/scratch/keep.cbx --target 1100110)
run("13 bits.*beyond this Bomb; the Bomb of 14-bit strings can say.*Verification: PASS"
    ${MATRIX} ask --file ${W}/scratch/keep.cbx --target 0110100110010)
foreach(bad "01x0" "01101001100101")
  execute_process(COMMAND ${MATRIX} ask --file ${W}/scratch/keep.cbx --target ${bad}
                  RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(code EQUAL 0)
    message(FATAL_ERROR "the target ${bad} was taken:\n${out}${err}")
  endif()
endforeach()

# Nothing of this writes to the Bomb.
file(SHA256 ${W}/lad/fixed-run-13.frb after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "the Bomb changed")
endif()
file(REMOVE_RECURSE ${W})
