# End-to-end check of `carlobomb bomb`.  Run by ctest:
#   cmake -DCLI=<carlobomb> -DWORK=<dir> -DHAVE_ASM=ON|OFF -DMODE=small|16|16full
#         -DFIXTURE=<compact_fixture> -P bomb_smoke.cmake
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

function(expect_same_file a b)
  file(SHA256 ${a} hash_a)
  file(SHA256 ${b} hash_b)
  if(NOT hash_a STREQUAL hash_b)
    message(FATAL_ERROR "${a} and ${b} differ")
  endif()
endfunction()

function(expect_size file expected)
  file(SIZE ${file} actual)
  if(NOT actual EQUAL expected)
    message(FATAL_ERROR "${file}: ${actual} bytes, expected ${expected}")
  endif()
endfunction()

# A complete fixed-width Bomb into the compact format and back: the compact
# file verifies on its own and against its source, answers like it, and
# expands to the same bytes.  The source is the same file afterwards.
function(compact_round_trip source name source_version bits)
  file(SHA256 ${source} before)
  run(0 "format version ${source_version}, ${bits}-bit targets.*validated in full.*Compact: .*navigation bits.*% of the source.*Every string of ${bits} bits has the REV here that the source has for it.*The source is unchanged"
      bomb compact --file ${source} --output ${W}/${name}.frb)
  run(0 "Compact format \\(version 4\\), ${bits}-bit targets.*made from a version-${source_version} file.*every navigation bit[^\n]*PASS.*Every REV decoded, its colors found[^\n]*PASS.*checksum of its source[^\n]*PASS.*every comparison of two extensions [.]+ PASS.*REVs that are not the ones of that file: 0 [.]+ PASS.*Verification: PASS \\(complete certified table, compact format\\)"
      bomb verify --file ${W}/${name}.frb --against ${source})
  run(0 "validated in full.*Wrote .*format version ${source_version}, ${bits}-bit targets.*the same.table, byte for byte"
      bomb expand --file ${W}/${name}.frb --output ${W}/${name}-expanded.frb)
  expect_same_file(${W}/${name}-expanded.frb ${source})
  expect_sha256(${source} ${before})
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
  run(2 "bits must be 1..31" bomb build --bits 32 --work-dir ${W}/x)
  run(2 "table format 1 cannot hold 17-bit targets" bomb build --bits 17 --format 1 --work-dir ${W}/x)
  run(2 "table format 2 cannot hold 25-bit targets" bomb build --bits 25 --format 2 --work-dir ${W}/x)
  run(2 "--format must be 1, 2 or 3" bomb build --bits 10 --format 4 --work-dir ${W}/x)
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

  # The ladder: from the 10-bit table to 13 bits, stopped once on the way.
  run(3 "Level 11.*Stopped at the requested job cap, sample size or time limit.*Run the same command again"
      bomb ladder --dir ${W}/lad --from ${W}/a.frb --to 13 --workers 2 --limit-jobs 40 --progress 0)
  run(0 "  10 +1024 +6 +[0-9a-f]+ +1 .*Level 11 is under construction" bomb ladder --dir ${W}/lad --status)
  run(3 "built on +the complete 10-bit Bomb.*REV of the 10-bit Bomb +1,024.*NOT COMPLETE"
      bomb status --work-dir ${W}/lad/work-11)
  run(0 "continuing from it.*Level 11:[^\n]*continued.*FixedRunBomb\\(13\\) is complete.*The ladder is complete"
      bomb ladder --dir ${W}/lad --to 13 --workers 2 --progress 0)
  run(0 "nothing to do.*The ladder is complete" bomb ladder --dir ${W}/lad --to 13 --progress 0)
  run(0 "  13 +8192 +7 +[0-9a-f]+ +1 " bomb ladder --dir ${W}/lad --status)
  run(0 "Against the complete 10-bit Bomb.*PASS.*against the journal[^\n]*PASS.*8192 certified minima reproduced.*The hardest 13-bit strings.*0000000000001 needs +7: it is among the hardest.*Verification: PASS \\(complete certified table\\)"
      bomb verify --file ${W}/lad/fixed-run-13.frb --work-dir ${W}/lad/work-13 --against ${W}/a.frb --recertify --engine c)
  run(0 "The 2 strings certified at the largest minimum, 7 nodes, recomputed.*engine c: PASS.*2 targets solved again.*2 certified minima reproduced"
      bomb verify --file ${W}/lad/fixed-run-13.frb --hardest --engine c)
  run(0 "the first 7 bits alone need|by the 10-bit Bomb|status +MINIMAL" bomb status --work-dir ${W}/lad/work-11 --target 01101001101)
  # One level by hand, on a base Bomb, in the 24-bit format.
  run(0 "FixedRunBomb\\(11\\) on the complete 10-bit Bomb.*COMPLETE"
      bomb build --bits 11 --work-dir ${W}/onbase --base ${W}/a.frb --format 2 --progress 0)
  run(0 "Wrote .*: 69696 bytes.*COMPLETE" bomb finalize --work-dir ${W}/onbase --output ${W}/v2-11.frb)
  run(0 "Format version 2, 11-bit targets.*Against the complete 10-bit Bomb.*PASS.*Verification: PASS"
      bomb verify --file ${W}/v2-11.frb --work-dir ${W}/onbase --against ${W}/a.frb)
  run(0 "Result: MINIMAL.*Verification: PASS" fixed --bomb ${W}/v2-11.frb --target 0110100110)
  run(2 "--against takes a Bomb for fewer bits" bomb verify --file ${W}/a.frb --against ${W}/v2-11.frb)
  run(1 "Against [^\n]*a-partial.frb: FAIL \\(table is not complete" bomb verify --file ${W}/v2-11.frb --against ${W}/a-partial.frb)
  # The 31-bit format, and a ladder that crosses from one format into the next:
  # levels 11 and 12 in version 2, then 13 in version 3 on the version-2 table.
  run(0 "The ladder is complete" bomb ladder --dir ${W}/lad3 --from ${W}/a.frb --to 12 --format 2 --workers 2 --progress 0)
  run(0 "continuing from it.*FixedRunBomb\\(13\\) is complete.*The ladder is complete"
      bomb ladder --dir ${W}/lad3 --to 13 --format 3 --workers 2 --progress 0)
  run(0 "  12 +4096 +6 +[0-9a-f]+ +2 .*  13 +8192 +7 +[0-9a-f]+ +3 " bomb ladder --dir ${W}/lad3 --status)
  run(0 "Format version 3, 13-bit targets.*Against the complete 12-bit Bomb.*PASS.*against the journal[^\n]*PASS.*8192 certified minima reproduced.*Verification: PASS \\(complete certified table\\)"
      bomb verify --file ${W}/lad3/fixed-run-13.frb --work-dir ${W}/lad3/work-13 --against ${W}/lad3/fixed-run-12.frb --recertify --engine c)
  run(0 "built on +the complete 12-bit Bomb.*journal format 3, table format 3.*COMPLETE" bomb status --work-dir ${W}/lad3/work-13)
  run(0 "Result: MINIMAL.*Verification: PASS" fixed --bomb ${W}/lad3/fixed-run-13.frb --target 0110100110011)
  run(0 "Status: MINIMAL.*Nodes: 7.*Verification: PASS" bomb lookup --file ${W}/lad3/fixed-run-13.frb --target 0000000000001)
  # 64 + 34 * 2^11 and 64 + 44 * 2^13 bytes.
  run(0 "Wrote .*: 69696 bytes" bomb finalize --work-dir ${W}/lad3/work-11 --output ${W}/v2-again.frb)
  run(0 "Wrote .*: 360512 bytes" bomb finalize --work-dir ${W}/lad3/work-13 --output ${W}/v3-again.frb)

  # The compact format: from each of the three fixed-width versions and back.
  compact_round_trip(${W}/a.frb c10 1 10)
  compact_round_trip(${W}/v2-11.frb c11 2 11)
  compact_round_trip(${W}/lad3/fixed-run-13.frb c13 3 13)
  # 448 bytes of header and directory; 64 bytes of navigation bits, 256 of node
  # counts and 48 of counts before for the 512 strings that start with 0; their
  # REVs, 1, 3, 32, 281, 194 and 1 of 1, 6, 15, 20, 35 and 23 bits, each class to a
  # whole number of 8 bytes; and 8 bytes.
  expect_size(${W}/c10.frb 2472)
  run(0 "Verification: PASS \\(complete certified table, compact format\\)" bomb verify --file ${W}/c10.frb)
  run(0 "Against the complete 10-bit Bomb[^\n]*PASS.*Verification: PASS" bomb verify --file ${W}/c13.frb --against ${W}/a.frb)
  # A compact Bomb answers every length up to its own: a string of its own
  # length with the REV of the fixed-width one, a shorter one that starts with 0
  # with the extension the FixedSolver chooses on the fixed-width one, and one
  # that starts with 1 with the complement of what its complement gets.
  run(0 "Status: MINIMAL.*Nodes: 4.*Verification: PASS" bomb lookup --file ${W}/c10.frb --target 0110100110 --out ${W}/c.rev)
  run(0 "Status: MINIMAL.*Nodes: 4.*Verification: PASS" bomb lookup --file ${W}/a.frb --target 0110100110 --out ${W}/f.rev)
  expect_same_file(${W}/c.rev ${W}/f.rev)
  run(0 "Verification: PASS" verify --target 0110100110 --graph ${W}/c.rev)
  run(0 "5 bits; .*compact, 10-bit Bomb.*Status: MINIMAL.*Extension: 0110110110: of the 10-bit strings.*fewest nodes, the lowest\n.*Nodes: 2.*Verification: PASS"
      bomb lookup --file ${W}/c10.frb --target 01101)
  run(0 "5 bits; .*compact, 10-bit Bomb.*Status: MINIMAL.*Extension: 1001001001: of the 10-bit strings.*fewest nodes, the highest.*Nodes: 2.*Verification: PASS"
      bomb lookup --file ${W}/c10.frb --target 10010)
  run(0 "Result: MINIMAL.*Nodes: 2.*Extension: 0110110110 .*Verification: PASS" fixed --bomb ${W}/c10.frb --target 01101)
  run(0 "Result: MINIMAL.*Nodes: 2.*Extension: 1001001001 .*Verification: PASS" fixed --bomb ${W}/c10.frb --target 10010)
  run(0 "Result: MINIMAL.*Nodes: 2.*Extension: 0110110110 .*Verification: PASS" fixed --bomb ${W}/a.frb --target 01101)
  run(0 "Result: MINIMAL.*Verification: PASS" fixed --bomb ${W}/c13.frb --target 0110100110011)
  run(3 "Result: UNSUPPORTED.*no REV with fewer than 4 nodes emits it" fixed --bomb ${W}/c10.frb --target 011010011001)
  run(3 "Result: UNSUPPORTED.*no REV with fewer than 4 nodes emits it" fixed --bomb ${W}/a.frb --target 011010011001)
  # Which of the two extensions of a string needs fewer nodes: worked out on
  # demand from the compact file, for a string and for its complement.
  run(0 "Target: 0101010 \\(7 bits; .*compact, 10-bit Bomb\\)\nFollowed by 0: 4 nodes\nFollowed by 1: 2 nodes\nFewer nodes: 1 \\(by 2\\)"
      bomb compare --file ${W}/c10.frb --target 0101010)
  run(0 "Followed by 0: 2 nodes\nFollowed by 1: 4 nodes\nFewer nodes: 0 \\(by 2\\)" bomb compare --file ${W}/c10.frb --target 1010101)
  run(0 "Followed by 0: 4 nodes\nFollowed by 1: 4 nodes\nFewer nodes: neither" bomb compare --file ${W}/c10.frb --target 1100110)
  run(0 "Followed by 0: 1 nodes\nFollowed by 1: 2 nodes\nFewer nodes: 0 \\(by 1\\)" bomb compare --file ${W}/c10.frb --target 0)
  run(2 "only\na Bomb of 11-bit strings can compare them" bomb compare --file ${W}/c10.frb --target 0110100110)
  run(2 "holds targets of up to 10 bits" bomb compare --file ${W}/c10.frb --target 01101001100)
  run(2 "is not a Bomb in the compact format" bomb compare --file ${W}/a.frb --target 01101)
  run(2 "compare needs --file and --target" bomb compare --file ${W}/c10.frb)
  # A compact file that holds this table and names another as its source:
  # valid in every byte, and found out by the two commands that ask.
  execute_process(COMMAND ${FIXTURE} ${W}/a.frb ${W}/c10-false.frb RESULT_VARIABLE fixture_code)
  if(NOT fixture_code EQUAL 0)
    message(FATAL_ERROR "compact_fixture failed: ${fixture_code}")
  endif()
  run(1 "Every REV decoded[^\n]*PASS.*The table it holds has the checksum of its source[^\n]*FAIL.*Verification: FAIL"
      bomb verify --file ${W}/c10-false.frb)
  run(1 "it says it was made from a version-1 file with table CRC-32.*Nothing was written"
      bomb expand --file ${W}/c10-false.frb --output ${W}/no.frb)
  run(0 "Status: MINIMAL.*Nodes: 4.*Verification: PASS" bomb lookup --file ${W}/c10-false.frb --target 0110100110)
  # Compact refusals: nothing is replaced, nothing partial is written, and a
  # construction does not take a compact file for its base.
  run(2 "unknown option --index-bytes" bomb compact --file ${W}/a.frb --output ${W}/no.frb --index-bytes 4)
  run(3 "partial table: only a complete Bomb can be written in the compact format.*Nothing was written"
      bomb compact --file ${W}/a-partial.frb --output ${W}/no.frb)
  run(2 "c10.frb exists.  A conversion never replaces a file" bomb compact --file ${W}/a.frb --output ${W}/c10.frb)
  run(2 "a.frb exists.  A conversion never replaces a file" bomb compact --file ${W}/a.frb --output ${W}/a.frb)
  run(2 "a.frb exists.  A conversion never replaces a file" bomb expand --file ${W}/c10.frb --output ${W}/a.frb)
  run(2 "is in the compact format already" bomb compact --file ${W}/c10.frb --output ${W}/no.frb)
  run(2 "is not a Bomb in the compact format" bomb expand --file ${W}/a.frb --output ${W}/no.frb)
  run(2 "compact needs --file and --output" bomb compact --file ${W}/a.frb)
  run(2 "cannot read" bomb compact --file ${W}/missing.frb --output ${W}/no.frb)
  run(2 "check a fixed-width file against how it was built" bomb verify --file ${W}/c10.frb --work-dir ${W}/a)
  run(2 "takes a Bomb for the same or fewer bits" bomb verify --file ${W}/c10.frb --against ${W}/v2-11.frb)
  run(2 "is in the compact format.  To compare the two files, verify the compact one" bomb verify --file ${W}/a.frb --against ${W}/c10.frb)
  # b.frb certifies the same minima as a.frb with other REVs: every answer of
  # the compact file is right by it, and it is not the file this one was made from.
  run(1 "every comparison of two extensions [.]+ PASS.*REVs that are not the ones of that file: [1-9][0-9,]*[^\n]*FAIL \\(the header says otherwise\\).*was not made from that file.*Verification: FAIL"
      bomb verify --file ${W}/c10.frb --against ${W}/b.frb)
  run(0 "REVs that are not the ones of that file: 0 [.]+ PASS" bomb verify --file ${W}/c11.frb --against ${W}/lad3/fixed-run-11.frb)
  # The same REVs in another fixed-width format: right in every answer, and said
  # not to be the file the compact one names as its source.
  run(0 "REVs that are not the ones of that file: 0 [.]+ PASS.*note: that file is format version 1 with table CRC-32 [0-9a-f]+; the file this one was\n  made from is version 3 with table CRC-32 [0-9a-f]+: the same REVs in another fixed-width format.*Verification: PASS"
      bomb verify --file ${W}/c13.frb --against ${W}/lad/fixed-run-13.frb)
  execute_process(COMMAND ${CLI} bomb verify --file ${W}/c13.frb --against ${W}/lad3/fixed-run-13.frb
                  OUTPUT_VARIABLE own_source ERROR_VARIABLE own_source RESULT_VARIABLE own_code)
  if(NOT own_code EQUAL 0 OR own_source MATCHES "note: that file")
    message(FATAL_ERROR "a compact file checked against its own source: ${own_source}")
  endif()
  # b.frb was built without the symmetry: the REV of a string that starts with 1
  # is not the complement of its complement's.  Its compact file keeps the half
  # that starts with 0, says how many strings get another REV for it, and
  # expands to a complete Bomb that is not b.frb.
  run(0 "[1-9][0-9,]* of the 1,024 strings of 10 bits have another REV here than in the source.*The source is unchanged"
      bomb compact --file ${W}/b.frb --output ${W}/cb.frb)
  run(0 "REVs that are not those of its source, by its own count [.]+ [1-9].*REVs that are not the ones of that file: [1-9][0-9,]*[^\n]*PASS \\(as the header says\\).*Verification: PASS"
      bomb verify --file ${W}/cb.frb --against ${W}/b.frb)
  run(0 "of its REVs are not those of the file the compact one was made from.*a complete Bomb, and not that file"
      bomb expand --file ${W}/cb.frb --output ${W}/cb-expanded.frb)
  run(0 "Verification: PASS \\(complete certified table\\)" bomb verify --file ${W}/cb-expanded.frb)
  run(0 "Against the complete 10-bit Bomb[^\n]*PASS" bomb verify --file ${W}/c13.frb --against ${W}/cb-expanded.frb)
  file(SHA256 ${W}/b.frb sha_b)
  file(SHA256 ${W}/cb-expanded.frb sha_cb)
  if(sha_b STREQUAL sha_cb)
    message(FATAL_ERROR "the expansion of cb.frb is b.frb, which has other REVs")
  endif()
  run(2 "holds targets of up to 10 bits" bomb lookup --file ${W}/c10.frb --target 01101001100)
  run(2 "is in the compact format.  A construction builds on a fixed-width file.*bomb expand"
      bomb ladder --dir ${W}/nolad --from ${W}/c10.frb --to 11)
  run(2 "is in the compact format.  A construction builds on a fixed-width file"
      bomb build --bits 11 --work-dir ${W}/nobase --base ${W}/c10.frb)
  if(EXISTS ${W}/no.frb OR EXISTS ${W}/no.frb.tmp OR EXISTS ${W}/nolad OR EXISTS ${W}/nobase)
    message(FATAL_ERROR "a refused command left something behind")
  endif()
  expect_sha256(${W}/a.frb ${sha_a})

  # Migration: the compact file of every complete Bomb of a ladder directory, in
  # a directory of its own.  The ladder directory is the same afterwards.
  file(GLOB before_migration RELATIVE ${W}/lad3 ${W}/lad3/*)
  file(SHA256 ${W}/lad3/fixed-run-13.frb sha_13)
  run(0 "The fixed-width files are only read.*11 bits: reading and validating.*  11 +69,696 +[0-9,]+ +[0-9.]+% +0 .*  12 +139,328 +[0-9,]+ +[0-9.]+% +0 .*  13 +360,512 +[0-9,]+ +[0-9.]+% +0 .*Nothing in [^\n]*lad3 was written, renamed or removed"
      bomb migrate --dir ${W}/lad3 --output ${W}/v4)
  file(GLOB after_migration RELATIVE ${W}/lad3 ${W}/lad3/*)
  if(NOT "${before_migration}" STREQUAL "${after_migration}")
    message(FATAL_ERROR "the migration changed the ladder directory: ${after_migration}")
  endif()
  expect_sha256(${W}/lad3/fixed-run-13.frb ${sha_13})
  expect_same_file(${W}/v4/fixed-run-13.v4.frb ${W}/c13.frb)
  run(0 "REVs that are not the ones of that file: 0 [.]+ PASS.*Verification: PASS"
      bomb verify --file ${W}/v4/fixed-run-12.v4.frb --against ${W}/lad3/fixed-run-12.frb)
  # again: nothing to do, and nothing is replaced
  run(0 "11 bits: [^\n]*is there already.*13 bits: [^\n]*is there already.*was there already"
      bomb migrate --dir ${W}/lad3 --output ${W}/v4)
  expect_same_file(${W}/v4/fixed-run-13.v4.frb ${W}/c13.frb)
  # a file of that name that is not the compact file of the Bomb is left alone, and the command fails
  file(MAKE_DIRECTORY ${W}/v4-other)
  file(COPY_FILE ${W}/c10.frb ${W}/v4-other/fixed-run-11.v4.frb)
  run(1 "11 bits: [^\n]*exists and is not the compact file of.*  11 +NOT MIGRATED.*  12 +139,328"
      bomb migrate --dir ${W}/lad3 --output ${W}/v4-other)
  expect_same_file(${W}/v4-other/fixed-run-11.v4.frb ${W}/c10.frb)
  # a partial table is skipped; a directory without Bombs, and the ladder's own directory, are refused
  file(MAKE_DIRECTORY ${W}/partial)
  file(COPY_FILE ${W}/a-partial.frb ${W}/partial/fixed-run-10.frb)
  run(0 "10 bits: [^\n]*is a partial table: skipped" bomb migrate --dir ${W}/partial --output ${W}/v4-partial)
  run(2 "No fixed-run-L.frb in" bomb migrate --dir ${W}/v4 --output ${W}/v4-none)
  run(2 "The compact files go to a\ndirectory of their own" bomb migrate --dir ${W}/lad3 --output ${W}/lad3/)
  run(2 "migrate needs --dir" bomb migrate --dir ${W}/lad3)
  if(EXISTS ${W}/v4-partial/fixed-run-10.v4.frb OR EXISTS ${W}/lad3/fixed-run-13.v4.frb)
    message(FATAL_ERROR "a migration wrote a file it should not have")
  endif()

  # Ladder refusals.
  run(2 "ladder needs --dir" bomb ladder --to 12)
  run(2 "ladder needs --to" bomb ladder --dir ${W}/lad)
  run(2 "the ladder climbs to 2..31 bits" bomb ladder --dir ${W}/lad --to 32)
  run(2 "name a complete one with --from" bomb ladder --dir ${W}/empty --to 12)
  run(3 "is not a complete Bomb" bomb ladder --dir ${W}/empty --from ${W}/a-partial.frb --to 12)
  run(2 "is a Bomb for 10-bit targets; a construction for 9 bits needs a shorter one"
      bomb build --bits 9 --work-dir ${W}/x --base ${W}/a.frb)

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
  # The compact format: 129,680 bytes for the same 65,536 REVs, and back.
  compact_round_trip(${W}/fixed-run-16.frb c16 1 16)
  expect_size(${W}/c16.frb 129680)
  run(0 "16 bits; .*compact, 16-bit Bomb.*Status: MINIMAL.*Nodes: 6.*Verification: PASS"
      bomb lookup --file ${W}/c16.frb --target 0100100011010110 --out ${W}/gc.rev)
  expect_same_file(${W}/gc.rev ${W}/g.rev)

elseif(MODE STREQUAL "16full")
  run(0 "Complete: 15,382 of 65,536 targets are emitted by a graph of at most 5 nodes.*the other 50,154 need at least 6.*COMPLETE: every target has a certified minimum.  The largest is 7 nodes"
      bomb build --bits 16 --max-nodes 16 --workers 4 --work-dir ${W}/w --progress 0)
  run(0 "COMPLETE" bomb finalize --work-dir ${W}/w --output ${W}/fixed-run-16.frb)
  expect_sha256(${W}/fixed-run-16.frb 0394d4d358312ae740ec1c05fbd0c18ca1416dfec3a2fcc400148a9b5e5e6a14)
  run(0 "65536 certified minima reproduced.*Verification: PASS \\(complete certified table\\)"
      bomb verify --file ${W}/fixed-run-16.frb --work-dir ${W}/w --recertify)
  # The compact file of a table is the same bytes on every machine as well.
  compact_round_trip(${W}/fixed-run-16.frb c16 1 16)
  expect_size(${W}/c16.frb 129680)
  expect_sha256(${W}/c16.frb c29f50ad6e4133ca06625f04e1f7acd6f0c7154c07d970d16637942e48e7f765)
  # One rung up.  The 17-bit table is the same bytes on every machine too.
  run(0 "FixedRunBomb\\(17\\) is complete.*      8 +2 +131,072 +100.00%.*The ladder is complete"
      bomb ladder --dir ${W}/lad --from ${W}/fixed-run-16.frb --to 17 --workers 4 --progress 0)
  expect_sha256(${W}/lad/fixed-run-17.frb e6d4cc43d3edc56f88e96037cef81f7f4c0b6e88ecbaef5052339596726eecdb)
  run(0 "Against the complete 16-bit Bomb.*PASS.*2000 certified minima reproduced.*Verification: PASS"
      bomb verify --file ${W}/lad/fixed-run-17.frb --work-dir ${W}/lad/work-17 --against ${W}/fixed-run-16.frb
      --recertify --sample 2000 --engine c)
  compact_round_trip(${W}/lad/fixed-run-17.frb c17 2 17)
  expect_size(${W}/c17.frb 253192)
  expect_sha256(${W}/c17.frb 3c4a7ba7eaa6737374e603413b1aa8b2136efc6e643cce9ef9cf98ca0cd0225b)
  run(0 "Against the complete 16-bit Bomb[^\n]*PASS.*is 8 nodes, needed by 2 strings.*Verification: PASS"
      bomb verify --file ${W}/c17.frb --against ${W}/fixed-run-16.frb)

else()
  message(FATAL_ERROR "MODE must be small, 16 or 16full")
endif()

file(REMOVE_RECURSE ${W})
