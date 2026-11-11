# LevelSolver — AMD64 assembly challenger

Status: **experimental**. Correct on every test and faster than the C
baseline on every measured workload, but not promoted; the C solver in
`solvers/level/fastest/c` remains the default engine.

| File | Role |
|---|---|
| `search_sysv.S` | The search kernel. GNU as, Intel syntax, System V AMD64 ABI. |
| `asm_solver.c/.h` | C wrapper: validation, graph export and completion, independent verification, public API. |
| `asm_layout.h` | State layout shared by C and assembly, checked with `_Static_assert`. |

Built only for Linux x86-64 (`CARLOBOMB_AMD64_ASM`, default ON there). On any
other platform it is not compiled and `--engine amd64-asm` is rejected.

```bash
./build/carlobomb level --target 010010001 --nodes 4 --engine amd64-asm
./build/carlobomb solve --target 010010001 --engine amd64-asm
```

## Contract

`rev_level_solve_amd64_ex` has the signature and behavior of
`rev_level_solve_ex`. For the same input the two return the same status, the
same graph (after completion), the same step count, the same peak stack depth
and the same limit reason. `tests/test_asm_equivalence.c` enforces all of it.

The kernel is one call per search: `int cb_amd64_search(CbAsmState *, int
max_nodes)`. There are no callbacks, no allocation, and no calls out of the
kernel. State is static, as in the C solver, so it is not reentrant.

## How the kernel relates to the C state machine

The C solver keeps `(function, state)` in each frame and dispatches through a
`switch` on every step. The kernel keeps the same frames and the same steps
but represents a frame's state as a **position in the code**:

- When C pushes a frame, its next step is always that frame's `INIT`. The
  kernel continues straight into the INIT logic.
- When C pops a frame, the parent resumes in its `*_R` or `MATCH` state. The
  kernel jumps to the address the parent left in its frame slot, or simply
  falls through when the parent is known at assembly time.
- Values C stores in a frame but that can be read back from the graph (the
  loop counter `it` is the link it just wrote; `dir` is a color) are not
  stored.

What is not changed:

- **Order.** WHITE then BLACK; existing nodes in ascending index; then one new
  node; same undo actions in the same places.
- **Step count.** Every C step is counted (`STEP` / `STEPS n`), with the C
  state named in a comment beside it. The step limit stops the search at the
  same step.
- **Stack accounting.** Every C push is checked (`PUSHCHK`) against capacity
  and peak depth, in the same order relative to the steps around it. Each C
  frame owns one 16-byte slot of static storage, so depth, peak and the
  overflow point are the same numbers as in C, including for frames that are
  never materialized (below).
- **No new pruning.** A child frame that C would push and immediately pop is
  still counted step for step; it is just not written to memory.

## Optimization log

All figures are the geometric mean, over the seven benchmark targets with at
least 100,000 steps, of (C time / assembly time) using median times, with C
built by GCC 13.3 `-O3`, on the machine described in
`benchmarks/ASM_RESULTS.md`. Run-to-run variation is about ±2%.

| Stage | Commit | Change | Speedup vs GCC C | Kept |
|---|---|---|---|---|
| 1 | `9c61fcf` | Direct translation: central loop, jump table, one handler per C `case`. Byte-sized frame and node fields, hot variables in registers. | 1.5 | superseded |
| 2 | `de93d84` | Frame state as code position. An immediately failing CTRL child of a link attempt handled inline. | 2.7 | yes |
| 2b | `de93d84` | Same, but pop is `ret` on the machine stack (to use the return predictor). | 2.7 (1–3% above stage 2) | **no** |
| 3a | `b821530` | Target bit hoisted out of the link loop; link clears that the next attempt overwrites removed. | no measurable change | yes (less code) |
| 3b | `b821530` | FLOW INIT after a matched bit run inline; the CTRL frame it pushes examined before pushing. | 3.3 | yes |
| 4 | `b64e08f` | Consecutive steps with no push or success test between them counted with one subtraction. | 3.4 (about 2%) | yes |

Notes on the decisions:

- **Stage 1** shows what assembly buys with the algorithm's structure left
  alone: about 1.5x over GCC and 1.25x over Clang, from register allocation
  and compact data. The dispatch itself was still the cost.
- **Stage 2** removes most dispatches. An INIT that follows a push, and a
  `*_R` whose successor is fixed, need no indirect jump at all; only a pop
  into a parent that is not known at assembly time does.
- **Stage 2b was rejected.** Using `call`/`ret` was 1–3% faster on the heavy
  targets, which is at the edge of the noise, and slower on small ones (0.11
  vs 0.04 µs kernel time for `00000000`), most likely because leaving a deep
  search without returning desynchronizes the return predictor for the
  caller. It also moved up to 80 KB of frames onto the machine stack. Not
  worth it; the explicit static stack stays.
- **Stage 3b** follows from counting events in a model of the search
  (`001011011101111011111`, 502,057 steps): the commonest event by far is a
  FLOW frame pushing a CTRL frame whose INIT either fails at once (37,256
  times) or matches (35,673). Doing that INIT before deciding whether to push
  removes a frame write and an indirect jump from every one of the failures.
- **Stage 4** is exact, not approximate: when the limit falls inside a group
  of steps the search stops with `steps == limit` either way, and no other
  state is reported from a limit stop.

## Remaining bottlenecks

Hardware performance counters are not available on the benchmark machine, so
the following is reasoning from the code and the event counts, not a
measurement.

- **Color comparisons.** Whether the node reached has the target color is the
  search's actual decision and is close to a coin flip. In the model run
  there are about 112,000 such comparisons in 502,000 steps. If half
  mispredict at 15–20 cycles each, that alone is on the order of half of the
  kernel's time. No rearrangement of the same search removes those branches.
- **Pops through a frame.** Seven indirect jumps remain. Their targets depend
  on the search path and are only partly predictable.
- **Link loops over existing nodes.** Attempts on nodes of the wrong color
  are rejected one at a time. Keeping per-color node bit masks and stepping
  with `bsf` over only the viable nodes would turn those rejections into
  arithmetic (their steps would still be counted, in bulk). In the model run
  about 20,000 of 39,000 CTRL link attempts are such rejections. This was not
  attempted: it adds mask upkeep to the coloring paths and a careful fallback
  near the step limit, for an estimated gain of a few percent.
- **API overhead.** For targets solved in a few dozen steps the wrapper
  (validation, guard check, export, verification) costs more than the search:
  0.17 µs per run for `00000000` against 0.03 µs in the kernel.

## Memory safety

Sanitizers do not see inside the kernel, so its memory behavior is tested
directly:

- The node array and the frame array each sit between 64-byte guard
  patterns. The wrapper checks them after every search and reports
  `INTERNAL_ERROR` if one changed.
- The test runs the kernel on a state block placed against an inaccessible
  page, at either end, so any access outside the block faults.
- Cases cover the full frame array (stack limit at capacity), the full node
  array (64 nodes in use) and stack capacities from 1 frame up.
- A small assembly caller checks that all callee-saved registers and `rsp`
  come back unchanged on each kind of exit.

Indices are bounded by construction: node indices are below the budget
(at most 64), and a frame slot is written only after `PUSHCHK` has compared
the stack position with the capacity.

## Platform notes

- Instructions: baseline x86-64 only. No SSE, AVX, BMI or other extensions;
  the disassembly of the linked kernel was checked for them.
- Position independent (`lea` with `rip`), non-executable stack note present.
- GNU as and Clang's integrated assembler produce byte-identical code for
  this file (2,223 bytes of `.text`).
- The kernel stores code addresses in frames and jumps to them. It carries no
  CET markers (`endbr64`), so a binary containing it is not marked
  IBT-compatible. Add them if the project ever enables CET enforcement.
- The `flow_color` path mirrors a case the C code handles but no input
  reaches (a FLOW frame's `cp` is always colored). It is kept for fidelity and
  is the one part of the kernel no test executes.
- One undo is likewise unreachable in practice: uncoloring a node in `MATCH`
  only ever applies to a node created by the same link attempt, which is
  removed right after. It is kept because the C code has it.

## Promotion

Per the project rule, a challenger moves to `fastest/` only after correctness
and a repeatable performance win are demonstrated. Both hold on the one
machine measured. Before promoting to a `fastest/amd64/` category it should
be measured on at least one other processor family (the results here are
from a single Intel Xeon VM), ideally with hardware counters.
