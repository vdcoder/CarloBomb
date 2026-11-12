# LevelSolver — AMD64 assembly challenger

Status: **experimental**. Correct on every test and faster than the C
baseline on every measured workload (roughly 3.3x against GCC-built C and
2.7x against Clang-built C on search-heavy targets, on one machine), but not
promoted; the C solver in `solvers/level/fastest/c` remains the default
engine.

| File | Role |
|---|---|
| `search_sysv.S` | The search kernel. GNU as, Intel syntax, System V AMD64 ABI. |
| `asm_solver.c/.h` | C wrapper: validation, graph export and completion, independent verification, public API. |
| `asm_layout.h` | State layout shared by C and assembly, checked with `_Static_assert`. |

This kernel is the **reference** for the later graph-representation
experiments in `../repr`, which are faster at most node budgets (engine
`amd64-auto`). It is kept unchanged: it is the simplest of the assembly
kernels, needs only baseline x86-64, and every other kernel is measured
against it. `asm_solver.c` holds the registry through which the kernels are
selected; this one is kernel 0 and the default.

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
- A frame slot holds two things: the address to resume at, and at most one
  saved value (the previous `fp` or `cp`, or a pointer to the link being
  tried, which stands for C's `dir`). What can be read back from the graph is
  not stored: the loop counter `it` is the value last written to the link,
  and `set` is implied by which code position the frame is at.

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
`benchmarks/ASM_RESULTS.md`. Back-to-back runs agree to about 1%; runs from
different sessions on this shared machine can differ by 5%, so only the
larger steps in this table are meaningful individually.

| Stage | Commit | Change | Speedup vs GCC C | Kept |
|---|---|---|---|---|
| 1 | `9c61fcf` | Direct translation: central loop, jump table, one handler per C `case`. Byte-sized frame and node fields, hot variables in registers. | 1.52–1.54 | superseded |
| 2 | `de93d84` | Frame state as code position. An immediately failing CTRL child of a link attempt handled inline. | 2.66–2.69 | yes |
| 2b | `de93d84` | Same, but pop is `ret` on the machine stack (to use the return predictor). | 2.72–2.74 | **no** |
| 3a | `b821530` | Target bit hoisted out of the link loop; link clears that the next attempt overwrites removed. | no measurable change | yes (less code) |
| 3b | `b821530` | FLOW INIT after a matched bit run inline; the CTRL frame it pushes examined before pushing. | 3.25 | yes |
| 4 | `b64e08f` | Consecutive steps with no push or success test between them counted with one subtraction. | 3.38–3.40 | yes (fewer instructions, exactly equivalent) |
| 5 | `d65b75e` | Cleanup, not an optimization: removed the transcription of FLOW's color loop, which no path reached any more. | 3.29–3.31 (no change beyond session noise) | yes |

Notes on the decisions:

- **Stage 1** shows what assembly buys with the algorithm's structure left
  alone: about 1.5x over GCC and 1.25x over Clang, from register allocation
  and compact data. The dispatch itself was still the cost.
- **Stage 2** removes most dispatches. An INIT that follows a push, and a
  `*_R` whose successor is fixed, need no indirect jump at all; only a pop
  into a parent that is not known at assembly time does.
- **Stage 2b was rejected.** Using `call`/`ret` was 1–3% faster on the heavy
  targets in two alternated pairs of runs, which is inside the noise, and
  clearly slower on small ones (0.11 vs 0.04 µs kernel time for `00000000`),
  most likely because leaving a deep search without returning desynchronizes
  the return predictor for the caller. It also moved up to 80 KB of frames onto the machine stack. Not
  worth it; the explicit static stack stays.
- **Stage 3b** follows from counting events in a model of the search
  (`001011011101111011111`, 502,057 steps): the most common event is a FLOW
  frame pushing a CTRL frame whose INIT, finding its link decided, either
  fails at once (37,256 times) or matches (35,673). Doing that INIT before
  deciding whether to push removes a frame write and an indirect jump from
  every one of the failures.
- **Stage 4** is exact, not approximate: when the limit falls inside a group
  of steps the search stops with `steps == limit` either way, and no other
  state is reported from a limit stop. Its measured gain is too small to
  separate from noise on this machine; it stays because it only removes
  instructions.
- **Stage 5** removed code, so it was re-measured like an optimization. The
  figures came out about 3% lower against GCC and 5% lower against Clang
  than stage 4's. Rebuilding stage 4 and running both back to back showed
  the same binary moving by more than that between rounds, and the two
  stages' results overlapping, so this is recorded as no measurable change.

## Remaining bottlenecks

Hardware performance counters are not available on the benchmark machine, so
the following is reasoning from the code and the event counts, not a
measurement.

- **Color comparisons.** Whether the node reached has the target color is the
  search's actual decision and is close to a coin flip. In the model run
  there are about 112,000 such comparisons in 502,000 steps. If half
  mispredict at 15–20 cycles each, that alone is on the order of half of the
  kernel's time. No rearrangement of the same search removes those branches.
- **Pops through a frame.** Nine indirect jumps remain (six unconditional
  pops and three conditional ones). Their targets depend on the search path
  and are only partly predictable.
- **Link loops over existing nodes.** Attempts on nodes of the wrong color
  are rejected one at a time. Keeping per-color node bit masks and stepping
  with `bsf` over only the viable nodes would turn those rejections into
  arithmetic (their steps would still be counted, in bulk). In the model run
  about 20,000 of 39,000 CTRL link attempts are such rejections. This was not
  attempted: it adds mask upkeep to the coloring paths and a careful fallback
  near the step limit, for an estimated gain of a few percent.
- **API overhead.** For targets solved in a few dozen steps the wrapper
  (validation, guard check, export, verification) costs more than the search:
  for `00000000`, 0.16 µs per run in the GCC build (0.09 µs in the Clang
  build) against 0.03 µs in the kernel.

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
- `test_asm_equivalence N` adds N randomized differential cases. Runs of
  2,000,000 (GCC build), 500,000 (Clang build) and 200,000 (GCC with
  ASan/UBSan) cases found no difference from the C solver.
- A small assembly caller checks that all callee-saved registers and `rsp`
  come back unchanged on each kind of exit.

Indices are bounded by construction: node indices are below the budget
(at most 64), and a frame slot is written only after `PUSHCHK` has compared
the stack position with the capacity.

## Paths the tests cannot reach

Three places in the search can never have an effect, by an invariant of the
search rather than by a gap in the tests. All three are present in the C
code.

1. **FLOW with an undecided color** (C: FLOW `LOOP_C` / `LOOP_C_R`). A FLOW
   frame is pushed either by a CTRL INIT that has just matched the color of
   the new `cp` against the target, or by a FLOW frame with the same `cp`, and
   no frame changes a color that was decided when it was pushed. So FLOW
   INIT always finds its color decided. **The kernel does not contain this
   path**, and relies on the invariant: the FLOW INIT that follows a match
   takes `d` from the target bit instead of reading the color back.
2. **CTRL INIT moving `cp` to an uncolored node that already existed.** The
   node a CTRL INIT moves `cp` to is uncolored only when it was created by
   the `TRY_N` step that pushed that very frame; any other node it can reach
   has been colored by then. (The node under `fp` can be uncolored; that is
   the ordinary `LOOP_C` case and is exercised.) C tests the new `cp` node
   for "uncolored" generically, and the kernel keeps that test in the three
   places it occurs (`.Lctrl_init`, `PEEK_CTRL`, the link loop). Those three
   branches are never taken.
3. **The color undo in `MATCH`.** It does execute, but by point 2 only for
   the node created by the same `TRY_N`, which the following `TRY_N_R`
   removes; a new node is initialized from scratch. Its effect cannot be
   observed. It is kept because C has it.

Evidence, beyond the argument above:

- A recursive model of the search found no FLOW frame with an undecided
  color, and no coloring of a pre-existing node at a match, in 10,234 cases
  (every target up to 10 bits at budgets 1 to 5, plus four heavier ones).
- A kernel with a trap instruction on each of the three branches of point 2
  passed the differential test with 500,000 extra random cases and the
  5-node oracle, so none of them was taken.
- A kernel with the undo of point 3 deleted passed the same runs.

Neither experimental kernel is in the repository.

## Platform notes

- Instructions: baseline x86-64 only. No SSE, AVX, x87, BMI or other
  extensions. This was checked on the disassembly of the whole object (475
  instructions: `mov`, `lea`, `add`, `sub`, `cmp`, `test`, `and`, `xor`,
  `inc`, `dec`, `neg`, `shl`, `shr`, `movsx`, `movzx`, `push`, `pop`,
  conditional and unconditional jumps, one `ret`, and alignment `nop`s).
- No `call` instructions; the kernel never leaves itself.
- Position independent (`lea` with `rip`), non-executable stack note present.
- GNU as and Clang's integrated assembler produce the same instructions at
  the same addresses (2,043 bytes of `.text`). The two objects differ in
  nine bytes, all in one run of alignment padding that is never executed
  (the assemblers choose different multi-byte `nop` forms).
- Out-of-line code (slow paths of `PUSHCHK`, conditional pops, pushes from
  `PEEK_CTRL`) is placed after the main body with `.subsection 1`; the
  symbol `cb_amd64_search` covers both parts.
- The kernel stores code addresses in frames and jumps to them. It carries no
  CET markers (`endbr64`), so a binary containing it is not marked
  IBT-compatible. Add them if the project ever enables CET enforcement.

## Promotion

Per the project rule, a challenger moves to `fastest/` only after correctness
and a repeatable performance win are demonstrated. Both hold on the one
machine measured. Before promoting to a `fastest/amd64/` category it should
be measured on at least one other processor family (the results here are
from a single Intel Xeon VM), ideally with hardware counters.
