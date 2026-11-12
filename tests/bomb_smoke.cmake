# End-to-end check of `carlobomb bomb`.  Run by ctest:
#   cmake -DCLI=<carlobomb> -DWORK=<dir> -DHAVE_ASM=ON|OFF -DMODE=small|16|16full
#         -P bomb_smoke.cmake
#
# small   every command and exit status on 10-bit tables, with interruption
# 16      the 16-bit table with enumeration up to 4 nodes (seconds)
# 16full  the 16-bit table with the default settings, as the owner runs it

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

function(expect_sha256 file expected)
  file(SHA256 ${file} actual)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "${file}: SHA-256 ${actual}, expected ${expected}")
  endif()
endfunction()

set(W ${WORK}/bomb_${MODE})
file(REMOVE_RECURSE ${W})
file(MAKE_DIRECTORY ${W})

if(MODE STREQUAL "small")
  # A construction that is stopped after 20 jobs.
  run(3 "NOT COMPLETE.*Continue with: carlobomb bomb resume"
      bomb build --bits 10 --work-dir ${W}/a --enumerate-max 3 --limit-jobs 20 --progress 0)
  run(3 "NOT COMPLETE: [0-9,]+ of 1,024 targets have no certified minimum" bomb status --work-dir ${W}/a)
  # It cannot be finalized as complete, and nothing is written when it is refused.
  run(3 "a complete table cannot be written.*Nothing was written"
      bomb finalize --work-dir ${W}/a --output ${W}/a.frb)
  if(EXISTS ${W}/a.frb)
    message(FATAL_ERROR "finalize wrote a file it refused to write")
  endif()
  # It can be written as what it is, and reads as that.
  run(3 "PARTIAL: [0-9,]+ of 1,024 targets have no certified minimum"
      bomb finalize --work-dir ${W}/a --output ${W}/a-partial.frb --partial)
  run(3 "Verification: PASS \\(partial table\\)" bomb verify --file ${W}/a-partial.frb)
  run(3 "Verification: PASS \\(partial table\\)" bomb verify --file ${W}/a-partial.frb --work-dir ${W}/a)
  run(0 "Status: MINIMAL.*Table: PARTIAL.*Nodes: 1.*Verification: PASS"
      bomb lookup --file ${W}/a-partial.frb --target 1111111111)
  run(3 "Status: WITNESS.*Nodes: 10.*Verification: PASS"
      bomb lookup --file ${W}/a-partial.frb --target 0110100110)
  run(3 "status +WITNESS.*witness +10 nodes, from the ring construction.*lower bound +4"
      bomb status --work-dir ${W}/a --target 0110100110)

  # Resume, twice: the second time there is nothing left to do.
  run(0 "COMPLETE: every target has a certified minimum.*Next: carlobomb bomb finalize"
      bomb resume --work-dir ${W}/a --progress 0)
  run(0 "Session: 0 jobs.*COMPLETE" bomb resume --work-dir ${W}/a --progress 0)
  run(0 "status +MINIMAL.*1\\.\\.3 by exhaustive enumeration.*search at node budget"
      bomb status --work-dir ${W}/a --target 0110100110)
  run(0 "Wrote .*a.frb: 19520 bytes.*COMPLETE: every target has a certified minimum.  The largest is 6 nodes"
      bomb finalize --work-dir ${W}/a --output ${W}/a.frb)
  run(0 "Header, length, checksums, counts and flags [.]+ PASS.*Complement symmetry[^\n]* PASS.*against the journal[^\n]*PASS.*recomputed from node budget 1, engine c: PASS.*1024 certified minima reproduced.*Verification: PASS \\(complete certified table\\)"
      bomb verify --file ${W}/a.frb --work-dir ${W}/a --recertify --engine c)
  run(0 "Verification: PASS \\(complete certified table\\)" bomb verify --file ${W}/a.frb --recertify --sample 50)
  # A looked-up graph is an ordinary REV file for the older commands.
  run(0 "Status: MINIMAL.*Nodes: 3.*Verification: PASS.*Graph written"
      bomb lookup --file ${W}/a.frb --target 0100100010 --out ${W}/g.rev)
  run(0 "Nodes: 3.*Verification: PASS" verify --target 0100100010 --graph ${W}/g.rev)
  run(0 "^0100100010" run --graph ${W}/g.rev --bits 10)

  # The same table built other ways certifies the same minima.  The file may
  # hold other witnesses of the same size, so compare through the journal of
  # the first construction rather than byte for byte.
  run(0 "COMPLETE" bomb build --bits 10 --work-dir ${W}/b --enumerate-max 0 --symmetry off
      --engine c --workers 3 --progress 0)
  run(0 "COMPLETE" bomb finalize --work-dir ${W}/b --output ${W}/b.frb)
  run(0 "against the journal[^\n]*PASS.*Verification: PASS \\(complete" bomb verify --file ${W}/b.frb --work-dir ${W}/a)
  # Same settings, other worker count: the same file.
  run(0 "^$" bomb build --bits 10 --work-dir ${W}/c --enumerate-max 3 --workers 4 --quiet)
  run(0 "COMPLETE" bomb finalize --work-dir ${W}/c --output ${W}/c.frb)
  file(SHA256 ${W}/a.frb sha_a)
  file(SHA256 ${W}/c.frb sha_c)
  if(NOT sha_a STREQUAL sha_c)
    message(FATAL_ERROR "interrupted single-worker build and 4-worker build differ: ${sha_a} ${sha_c}")
  endif()

  # Step limits: an unfinished search is reported as unfinished, and a
  # larger limit picks up where the smaller one stopped.
  run(3 "search stopped by a resource limit +1,0[0-9][0-9].*NOT COMPLETE.*These settings can do no more"
      bomb build --bits 10 --work-dir ${W}/d --enumerate-max 0 --max-steps 60 --progress 0)
  run(3 "was stopped by the step limit \\(60\\).*that excludes nothing.*PROVEN_UNSAT.*RESOURCE_LIMIT \\(step limit 60\\)"
      bomb status --work-dir ${W}/d --target 0110100110)
  run(3 "Session: 0 jobs" bomb resume --work-dir ${W}/d --progress 0)
  run(0 "step limit per search   3,000, then none.*COMPLETE"
      bomb resume --work-dir ${W}/d --max-steps 3000,0 --workers 2 --progress 0)
  run(0 "COMPLETE" bomb finalize --work-dir ${W}/d --output ${W}/d.frb)
  run(0 "against the journal[^\n]*PASS" bomb verify --file ${W}/d.frb --work-dir ${W}/a)

  # A sample of the open targets, as for a pilot run.
  run(3 "stage B[^\n]*: 30/30 targets.*Stopped at the requested job cap, sample size or time limit"
      bomb build --bits 10 --work-dir ${W}/e --enumerate-max 3 --sample 30 --seed 7 --progress 0)

  # Refusals.
  run(2 "already holds a construction" bomb build --bits 10 --work-dir ${W}/a)
  run(2 "fixed for the life of a work directory" bomb resume --work-dir ${W}/a --bits 12)
  run(2 "fixed for the life of a work directory" bomb resume --work-dir ${W}/a --symmetry off)
  run(2 "fixed for the life of a work directory" bomb resume --work-dir ${W}/a --enumerate-max 4)
  run(2 "no FixedRunBomb construction here" bomb resume --work-dir ${W}/nothing)
  run(2 "no FixedRunBomb construction here" bomb status --work-dir ${W}/nothing)
  run(2 "bits must be 1..16" bomb build --bits 17 --work-dir ${W}/x)
  run(2 "max-nodes must be 1..16" bomb build --bits 10 --max-nodes 17 --work-dir ${W}/x)
  run(2 "enumerate-max must be 0..6" bomb build --bits 10 --enumerate-max 7 --work-dir ${W}/x)
  run(2 "workers must be" bomb build --bits 10 --workers 0 --work-dir ${W}/x)
  run(2 "bad value for --max-steps" bomb build --bits 10 --max-steps 5,x --work-dir ${W}/x)
  run(2 "unknown engine" bomb build --bits 10 --engine fortran --work-dir ${W}/x)
  if(EXISTS ${W}/x)
    message(FATAL_ERROR "a refused build left a work directory behind")
  endif()
  run(2 "holds 10-bit targets" bomb lookup --file ${W}/a.frb --target 010)
  run(2 "invalid target" bomb lookup --file ${W}/a.frb --target 01x)
  run(2 "this construction is for 10-bit strings" bomb status --work-dir ${W}/a --target 010)
  run(2 "cannot read" bomb verify --file ${W}/nothing.frb)
  run(2 "cannot read" bomb lookup --file ${W}/nothing.frb --target 0000000000)
  run(1 "Verification: FAIL \\(not a FixedRunBomb file\\)" bomb verify --file ${W}/a/config.txt)
  run(1 "Verification: FAIL \\(not a FixedRunBomb file\\)" bomb lookup --file ${W}/a/config.txt --target 0000000000)
  run(2 "unknown bomb command" bomb frobnicate)
  run(2 "bomb needs a command" bomb)
  run(2 "unknown option" bomb status --work-dir ${W}/a --frob 1)
  run(0 "carlobomb bomb build" bomb help)

  if(HAVE_ASM)
    run(0 "engine amd64-auto.*COMPLETE" bomb build --bits 9 --work-dir ${W}/f --enumerate-max 2 --progress 0)
    run(0 "engine amd64-hybx" bomb resume --work-dir ${W}/f --engine amd64-hybx --progress 0)
  else()
    # Without the assembly solver the default is C, and asking for an
    # assembly engine falls back to C with a notice.
    run(0 "engine c.*COMPLETE" bomb build --bits 9 --work-dir ${W}/f --enumerate-max 2 --progress 0)
    run(0 "with engine c,.*note: engine amd64-auto is not available here" bomb resume --work-dir ${W}/f --engine amd64-auto --progress 0)
  endif()

  # A table checked against the journal of some other construction.
  run(1 "the table is for 10-bit targets, the work directory for 9-bit ones.*Verification: FAIL"
      bomb verify --file ${W}/a.frb --work-dir ${W}/f)

elseif(MODE STREQUAL "16")
  run(0 "65,536 targets.*      7 +1,198 +65,536 +100.00%.*COMPLETE: every target has a certified minimum.  The largest is 7 nodes"
      bomb build --bits 16 --work-dir ${W}/w --enumerate-max 4 --workers 2 --progress 0)
  run(0 "Wrote .*: 1245248 bytes.*COMPLETE" bomb finalize --work-dir ${W}/w --output ${W}/fixed-run-16.frb)
  # Same settings give the same bytes on every machine, engine and worker count.
  expect_sha256(${W}/fixed-run-16.frb 3580e791c1b2985d595296c04ff0ed66d405019be9ee3f9d46c5b43e897b6e59)
  run(0 "16-bit targets, 65536 entries.*against the journal[^\n]*PASS \\(65536 backed.*      6 +48,956 +64,338.*Verification: PASS \\(complete certified table\\)"
      bomb verify --file ${W}/fixed-run-16.frb --work-dir ${W}/w)
  run(0 "entry 18646 .*Status: MINIMAL.*Nodes: 6.*Verification: PASS"
      bomb lookup --file ${W}/fixed-run-16.frb --target 0100100011010110 --out ${W}/g.rev)
  run(0 "Verification: PASS" verify --target 0100100011010110 --graph ${W}/g.rev)
  run(0 "Nodes: 6" solve --target 0100100011010110)

elseif(MODE STREQUAL "16full")
  run(0 "Complete: 15,382 of 65,536 targets are emitted by a graph of at most 5 nodes.*the other 50,154 need at least 6.*COMPLETE: every target has a certified minimum.  The largest is 7 nodes"
      bomb build --bits 16 --max-nodes 16 --workers 4 --work-dir ${W}/w --progress 0)
  run(0 "COMPLETE" bomb finalize --work-dir ${W}/w --output ${W}/fixed-run-16.frb)
  expect_sha256(${W}/fixed-run-16.frb 0394d4d358312ae740ec1c05fbd0c18ca1416dfec3a2fcc400148a9b5e5e6a14)
  run(0 "65536 certified minima reproduced.*Verification: PASS \\(complete certified table\\)"
      bomb verify --file ${W}/fixed-run-16.frb --work-dir ${W}/w --recertify)

else()
  message(FATAL_ERROR "MODE must be small, 16 or 16full")
endif()

file(REMOVE_RECURSE ${W})
