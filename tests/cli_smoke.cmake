# End-to-end check of the command-line tool.  Run by ctest:
#   cmake -DCLI=<carlobomb> -DWORK=<dir> -P cli_smoke.cmake

function(run expect_code expect_text)
  execute_process(COMMAND ${CLI} ${ARGN}
                  RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT code EQUAL expect_code)
    message(FATAL_ERROR "carlobomb ${ARGN}: exit ${code}, expected ${expect_code}\n${out}${err}")
  endif()
  if(NOT "${out}${err}" MATCHES "${expect_text}")
    message(FATAL_ERROR "carlobomb ${ARGN}: output lacks '${expect_text}'\n${out}${err}")
  endif()
endfunction()

set(graph ${WORK}/cli_smoke.rev)

run(0 "Nodes: 3.*Verification: PASS" solve --target 010010001 --max-nodes 10 --out ${graph})
run(0 "Verification: PASS" verify --target 010010001 --graph ${graph})
run(1 "Verification: FAIL.*bit 8" verify --target 010010000 --graph ${graph})
run(0 "^010010001" run --graph ${graph} --bits 9)
run(0 "Verification: PASS" level --target 010010001 --nodes 4)
run(1 "PROVEN_UNSAT" level --target 010010001 --nodes 2)
run(1 "at least 3 nodes" solve --target 010010001 --max-nodes 2)
run(3 "not a proof of impossibility" level --target 0110100110010110 --nodes 8 --max-steps 5)
run(3 "UNKNOWN" solve --target 0110100110010110 --max-steps 5)
run(2 "invalid target" solve --target 01201)
run(2 "must be 1.." level --target 0101 --nodes 0)
run(2 "usage" frobnicate)
run(0 "Status: MINIMAL" bench --target 010010001 --reps 3)
