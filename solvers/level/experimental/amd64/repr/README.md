# Graph-representation experiments (AMD64)

How should the search kernel store the REV graph? This directory holds the
experiments that answer it: one shared search body, many representations,
each built into its own kernel and measured against the reference kernel in
`../asm/search_sysv.S`.

The reference kernel is not modified. Its source and machine code are
byte-identical to what `benchmarks/ASM_RESULTS.md` measured (machine code
SHA-256 beginning `813d603827156ce1`); it remains engine `amd64-asm` and the
default assembly kernel.

## Outcome

Speedup over the reference assembly kernel, kernel time only, geometric mean
over target lengths 12 to 48, two runs (full tables:
`benchmarks/REPR_RESULTS.md`).

| Experiment | Kernel | N = 3 | N = 4 to 8 | Verdict |
|---|---|---|---|---|
| A. 16-bit packed nodes, memory | `p16m` | 0.88–0.89 | 0.90–0.93 | slower |
| B. 16-bit packed nodes, one GPR (N ≤ 4) | `p16g` | 0.69–0.70 | 0.86 at N = 4 | slower |
| B. color bitmaps + link nibbles, GPRs (N ≤ 8) | `n8g`, `n8s` | 0.95–1.02 | 1.05–1.11 | faster |
| B. the same with BMI2, links split | `n8s-bmi2` | 1.00–1.03 | 1.11–1.15 | faster |
| C. 16-bit packed nodes, XMM (N ≤ 8) | `p16x` | 0.51–0.52 | 0.64–0.69 | much slower |
| Color bitmaps in GPRs, links in memory (any N) | `hyb` | 1.03 | 1.03–1.05 | slightly faster |
| Frames 16 → 8 bytes | `hyb-f8`, `n8g-f8` | 0.93–0.99 | 1.02–1.06, no better than 16 | no gain |
| Bulk accounting on `n8s-bmi2` | `n8x` | 1.04–1.06 | **1.20–1.24** | best for N ≤ 8 |
| Bulk accounting on `hyb` | `hybx` | 1.00–1.02 | 1.11–1.14; 1.16–1.20 at N = 10 to 16 | best for N > 8 |
| Chosen per search | `auto` | 1.04 | **1.21–1.23** | |

Through the public API on the ten workloads of `ASM_RESULTS.md`, `auto` is
3.94x faster than GCC-built C and 3.25x faster than Clang-built C, where the
reference kernel measured 3.29x and 2.67x in the same session.

In short:

- **Compactness alone is not valuable.** The reference kernel's graph
  already fits in four cache lines. Packing it further adds shifts and masks
  to every access and turns every link write into a read-modify-write.
- **Register residence helps when the layout follows the access pattern**,
  not when the memory layout is moved into a register unchanged.
- **SIMD registers are the wrong home for one graph**: picking one element
  out of a vector by a run-time index costs more than an L1 load.
- **The largest single gain came from what bitmaps make possible**, not from
  where the bits live: skipping the certain failures of a link loop
  arithmetically.
- **All of it together is about 20%**, because most of the kernel's time is
  not graph access at all (see "Why the gains are bounded").

## How the experiments are built

`skel.inc` is the reference kernel's search with every graph access replaced
by a macro (`G_FP_D`, `G_LINK_CP`, `G_COLOR_TEST`, `G_LINK_SET_IT`, ...; the
list is in its header). A representation is a `g_*.inc` file that defines
those macros; a kernel is a few lines (`k_*.S`) naming a symbol, a
representation and options. So two kernels differ in the representation and
in nothing else.

`b4` is the control: the reference representation on the shared body. It
measures 1.00–1.03 against the reference kernel, so the shared body itself
neither helps nor hurts, and the other kernels' numbers are about their
representations.

Every kernel keeps the reference kernel's contract: same move order, same
first solution, same step count, same peak stack depth, same behavior at the
step and stack limits. The C wrapper, its kernel registry and the tests are
shared; a kernel that only handles small budgets names a fallback, so any
selection is valid for every budget.

| Kernel | Graph storage | Budgets | Needs |
|---|---|---|---|
| `asm` | reference: 4 bytes per node in memory | any | — |
| `b4` | the same, on the shared body | any | — |
| `p16m` | one 16-bit word per node (7 + 7 + 2 bits), in memory | any | — |
| `p16g` | the same words, four in one GPR | ≤ 4 | BMI2 |
| `p16x` | the same words, eight in `xmm0` | ≤ 8 | AVX-512BW/VL, BMI2 |
| `hyb` | colors: two bitmaps in GPRs; links: one byte each in memory | any | — |
| `n8g` | colors: bitmaps; links: 16 nibbles in one GPR, interleaved | ≤ 8 | — |
| `n8g-bmi2` | the same with SHLX/SHRX/ANDN | ≤ 8 | BMI2 |
| `n8s` | as `n8g`, link 0 nibbles in the low half, link 1 in the high half | ≤ 8 | — |
| `n8s-bmi2` | the same with BMI2 | ≤ 8 | BMI2 |
| `hyb-f8`, `n8g-f8` | `hyb` / `n8g` with 8-byte search frames | any / ≤ 8 | — |
| `n8x` | `n8s-bmi2` plus bulk accounting | ≤ 8 | BMI2 |
| `hybx` | `hyb` plus bulk accounting | any | BMI2 |
| `auto` | `n8x` up to 8 nodes and `hybx` above; without BMI2, `n8s` for 4 to 8 and `hyb` otherwise | any | — |

```bash
./build/carlobomb engines                                   # this list, with availability
./build/carlobomb solve --target 010010001 --engine amd64-auto
./build/repr_comparison                                     # the benchmark
./build/test_asm_equivalence 100000 n8x                     # test one kernel, with random cases
```

## Experiment A: 16-bit packed nodes in memory

`p16m` stores exactly the proposed word: link 0 in bits 0–6, link 1 in bits
7–13 (64 = undecided), color in bits 14–15. It is 7–12% slower than the
reference at every budget.

The reference kernel's node is four bytes, so 64 nodes are 256 bytes and the
working set of a search (graph plus a few dozen frames) is well inside L1
either way. Halving it changes nothing the processor notices. What packing
does change:

- a color or link read becomes load, shift, mask instead of load;
- a link write becomes load, clear field, insert, store instead of one byte
  store, and that happens on every iteration of a link loop;
- color and both links of a node now share a word, so writing one field
  makes the next read of any of them wait for the store.

## Experiment B: the graph in general-purpose registers

**The proposed layout, in a register (`p16g`).** Four 16-bit words in one
GPR, for budgets up to 4. It is slower than the reference: 0.86 at N = 4 and
0.70 at N = 3. A register removes the load, but the field still has to be
extracted with two shifts and a mask, a write is five or six operations, and
none of the tests the search actually performs gets simpler. (`p16g` goes
through the same word read/write helpers as `p16m` and `p16x`; hand-tuning
it would recover some of the loss but not change the order.)

**A layout made for the search (`n8g`, `n8s`).** The search asks three
questions over and over: what is the color of `fp`, where does link `d` of
`cp` lead, and does that node have the target color. Stored as

- two bitmaps, "color decided" and "is BLACK", one bit per node, and
- sixteen 4-bit links in one register (eight nodes, two links each; 15 =
  undecided),

the first is a bit test, the second one shift and a mask, and the third a
single bit test against a third register that holds "nodes whose color is
the target bit", prepared when the bit is fetched. Nothing on the path from
`(fp, cp)` to the comparison touches memory. This is 5–11% faster than the
reference for budgets 4 to 8.

**Splitting the links (`n8s`), and where BMI2 matters.** With link 0 of every
node in the low half of the register and link 1 in the high half, both links
of `cp` can be shifted out before the color of `fp` is known and one chosen
with a conditional move, which takes the color out of the address
computation. With SHRX the two shifts are independent and the kernel
(`n8s-bmi2`) reaches 1.11–1.15. Without BMI2 the second shift has to follow
the first and the gain over `n8g` disappears (`n8s`: 1.05–1.11). For the
interleaved layout BMI2 makes no consistent difference (`n8g-bmi2` vs
`n8g`). So BMI2 is worth using exactly where it lets two variable shifts
proceed in parallel, and for the bit scans of the bulk option below.

**Bitmaps with links left in memory (`hyb`).** Works for any budget and
needs no BMI2. Colors are read far more often than they change, links
change on every loop iteration, so this keeps the cheap one-byte link store
and still shortens the path to the comparison. 3–5% faster than the
reference.

**Backtracking updates.** In the register layouts a color undo is two bit
resets and a link write is a mask-and-insert of one nibble (five operations
with BMI, seven without). That is more than the reference's single store,
and it is why the register kernels gain less in link-heavy cells than the
shorter read path alone would suggest.

## Experiment C: the graph in an XMM register

`p16x` keeps eight 16-bit nodes in `xmm0`. Reading node `i` is VMOVQ,
VPERMW, VMOVD; writing it is a shift, KMOVD and a masked VPBROADCASTW. These
are the most direct forms AVX-512 offers. The kernel is 31–36% slower than
the reference for budgets 4 to 8 and 48% slower at N = 3.

The reason is the one anticipated: a vector register has no cheap
"element i" for a run-time `i`. Each read pays two crossings between the
integer and vector domains and a permute where the memory version pays one
load, and the search does three dependent reads per bit. YMM and ZMM versions would use the same
instructions with equal or higher latency, so they were not built.

Where vectors could pay is across searches rather than inside one graph;
see "Not attempted".

## Frame compression

The assembly kernels' frames are already 16 bytes (the C solver's are 32):
an address to resume at and one saved value. `hyb-f8` and `n8g-f8` shrink
them to 8 bytes (a 32-bit code offset and a 32-bit value). Against the
16-byte versions they measure the same or slightly slower (`hyb-f8`
0.98–1.05 where `hyb` is 1.03–1.05; `n8g-f8` 0.93–1.06 where `n8g` is
1.02–1.09). A search here is at most a few dozen frames deep, so the frame
array is in L1 at either size, and the smaller frame costs two extra
operations on every pop to rebuild the address. Not adopted.

## Bulk accounting

With colors in bitmaps, "which existing nodes have the target color" is a
register. In a CTRL link loop every attempt on a node of the wrong color
fails in the child's first step, so the kernel does not make those attempts
one at a time: a bit scan finds the next node of the right color, and the
failures before it are charged in one subtraction, three steps each, with
the stack depth they would have reached. The link is then set to that node
and the search continues exactly as before.

This is the same search: the same steps are counted, in the same order
relative to every success, and nothing is skipped that the per-node code
would have explored. Where doing it arithmetically could be observed (fewer
than 256 steps left before the step limit, the stack full, or an existing
node without a color) the kernel uses the per-node code instead.

It is worth 6–10% on top of the split layout (`n8x` 1.20–1.24 against
`n8s-bmi2` 1.11–1.15) and about the same on `hyb` (`hybx`), where it
reaches 1.16–1.20 at budgets 10 to 16.

**Tried and removed: the FLOW link loop.** There every existing node `n` is
tried as the new `fp`, and whether the child fails at once depends only on
the color of `n`, so all white nodes behave alike and all black nodes alike.
Skipping the certain failures there was implemented and measured 2–5%
*slower*, both with the two classes worked out on every iteration and with
them worked out once per loop. The attempts it skips were already cheap and
mostly well predicted, and the bookkeeping cost more than it saved. It is in
the history (commit `2b6200e`) and not in the kernels.

That experiment is also where the differential test earned its keep: the
first version read the link under trial as if it were decided when it
happened to be one of `cp`'s own links, misjudged which attempts were
certain to fail, and came out 4 steps short per occurrence. The test
reported it on the first run.

## By node budget and target length

`auto` against the reference kernel (run 1; all kernels and both runs in
`benchmarks/REPR_RESULTS.md`):

| N | L = 12 | L = 16 | L = 24 | L = 32 | L = 48 |
|---|---|---|---|---|---|
| 3 | 1.04 | 1.04 | 1.04 | 1.04 | 1.05 |
| 4 | 1.24 | 1.23 | 1.23 | 1.22 | 1.22 |
| 5 | 1.24 | 1.23 | 1.24 | 1.23 | 1.23 |
| 6 | 1.28 | 1.20 | 1.19 | 1.17 | 1.19 |
| 7 | 1.30 | 1.19 | 1.18 | 1.19 | 1.19 |
| 8 | 1.30 | 1.24 | 1.18 | 1.19 | 1.17 |

Target length makes little difference. The budget does: at N ≤ 3 there are
so few nodes that link loops are short and searches end quickly, and the
gain is a few percent. Beyond 8 nodes the links no longer fit one register;
`hybx` gives 1.16–1.20 at budgets 10 to 16.

## Why the gains are bounded

The same searches run at 1.3–1.8 ticks per step when the workload is small
enough for the branch predictor to memorize, and at 3.7–4.6 ticks per step
on a realistic workload (section 5 of the results file). Roughly 55–70% of
the kernel's time is therefore branch misprediction: at each bit the search
asks whether a node has the target color, the answer is close to a coin
flip, and the two outcomes lead to different code.

A representation can only do two things about that:

1. Let the mispredicted branch resolve sooner, by shortening the chain of
   operations that feeds it. That is what the register layouts do, and it is
   worth 5–15%.
2. Remove branches. That is what bulk accounting does for the one loop where
   the outcomes are known in advance from a bitmap, and it is worth another
   6–10%.

The other 30–45% is the instructions themselves, and there the reference
kernel is already the leanest: on the memorized workloads the register
kernels are slightly *slower* than it, because they execute more
operations per step.

## Selection at solver entry

`auto` picks a kernel once per search from the node budget and the CPU:

| CPU | N ≤ 3 | N = 4 to 8 | N > 8 |
|---|---|---|---|
| with BMI1/BMI2 | `n8x` | `n8x` | `hybx` |
| without | `hyb` | `n8s` | `hyb` |

The no-BMI2 column uses kernels that were measured here, but on a CPU that
has BMI2; it has not been run on one that lacks it.

`auto` is an engine beside `asm`, not a replacement for it. On searches of a
few hundred steps or less it is 4–10% slower than `asm` through the API.

## Correctness

Every kernel in the registry runs through the whole of
`tests/test_asm_equivalence.c` (23,002 differential cases against the C
solver, step and stack limits, guard bytes, guard pages, the
register-preservation check) and through the exhaustive oracle, in the
normal test suite. Beyond that, run by hand:

- 300,000 extra random cases on each of the fourteen kernels other than the
  reference, and 1,500,000 each on `n8x` and `hybx`: no differences.
- 200,000 each on `n8x` and `hybx` in the Clang build and 100,000 each
  under ASan/UBSan: no differences.
- The 5-node oracle on every kernel: no failures.

The reference kernel's own tests and results are unchanged.

## Limits of this evidence

- One processor (Intel Emerald Rapids) in a shared VM. Branch predictors and
  the relative cost of shifts, bit tests and loads differ between processor
  families; the ordering of the close results (`n8g` against `hyb`, BMI2
  against baseline shifts) could change elsewhere. The large results (A and
  C slower, `n8x` fastest for 4 to 8) have wide margins.
- No hardware counters. The misprediction share comes from the memorization
  experiment, not from a counter.
- Workloads are pseudo-random targets with a step cap. The ten fixed
  workloads of the earlier benchmark agree with them.
- The bulk option and the fastest kernels need BMI1/BMI2. A baseline
  version of bulk accounting (BSF and shifts by `cl`) is possible and was
  not written.

## Not attempted

- **Vectors across searches.** A batch of independent searches (every
  target of a given length, as a Bomb build would need) could run one search
  per vector lane, each lane a small state machine advanced with masked
  operations. That turns the unpredictable branches into data flow, which is
  the one thing no single-search kernel can do; the memorization experiment
  shows what is at stake (the scalar kernel runs about 2.5 times faster when
  its branches are predicted). It is a different kernel, not a variant of
  this one, and nothing here measures it.
- **Deciding several bits ahead without branching.** Evaluating the next two
  or three bits of a deterministic stretch with conditional moves and
  branching once on the outcome was worked through on paper and not built:
  by estimate, the extra operations per attempt cost about what the avoided
  mispredictions would save.
- **Bytes instead of nibbles for register links**, which would save one
  operation before the color test, and a baseline-ISA bulk option.
