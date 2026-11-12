# Multi-search SIMD for FixedRunBomb construction — design note

**Status: proposal. Nothing here is implemented or measured.** It records a
possible way to run many independent LevelSolver searches at once, what would
have to be true for it to pay, and what to measure before building it.
Figures marked *measured* come from `benchmarks/*_RESULTS.md`; everything else
is an estimate or an assumption and is labelled as such.

## 1. The workload

A FixedRunBomb for length L and node capacity N maps each of the 2^L binary
strings to a small REV. Built by search, that is one MinimalSolver run per
string: LevelSolver at budgets 1, 2, ... until one succeeds. The searches for
different strings share no state. That independence is what this note is
about.

Two reductions apply before any kernel work and are worth more than it:

- **Complement symmetry.** Flipping every color and exchanging link 0 with
  link 1 at every node turns a REV that emits S into one that emits the
  complement of S, with the same node count. (The two pointers follow the
  same path; only the emitted colors change.) Only half the strings need a
  search.
- **Enumerating graphs instead of searching per string**, for small N; see
  section 7. It may make the search kernel irrelevant up to N = 5 or 6.

## 2. Why run searches together

One search at a time, the kernel spends most of its time recovering from
branches the processor cannot predict. *Measured:* the reference kernel
needs 1.3–1.8 time-stamp ticks per step when its branches are predicted and
3.7–4.6 on a realistic workload. *Hypothesis, not yet confirmed with hardware
counters:* 55–70% of a search's time is misprediction.

A kernel that advances W searches in lockstep, each in its own vector lane,
makes every per-search decision a mask rather than a jump. Lanes that match
and lanes that do not go through the same instructions. If the hypothesis
holds, that removes the largest single cost; if it does not, most of the
case for this design goes with it. That is why the counter measurement comes
first (section 6).

## 3. Design

### Scope

Budgets up to 8 and targets up to 64 bits, the FixedRunBomb regime. Within
it one search's whole state is a handful of 64-bit words, which is what makes
lanes practical.

### Lanes

Eight 64-bit lanes per register with AVX-512, four with AVX2. Each lane is
one (target, budget) job. All per-search state is held as a structure of
arrays, one vector per field:

| Field | Bits per lane | Content |
|---|---|---|
| links | 64 | sixteen 4-bit links, the `n8s` layout of the scalar kernel |
| colors | 16 | "decided" and "is BLACK" bitmaps |
| position | ~20 | `fp`, `cp`, node count, `gen` |
| target | 64 | the lane's string, so the target bit is a shift, not a load |
| steps left | 64 | the exact logical step counter, counted down |
| stack | see below | |
| phase | few | what the lane does next |

Reads that the scalar kernel does with BT and SHRX become per-lane variable
shifts and mask tests (VPSRLVQ, VPTESTMQ); updates become masked blends.

### The stack: two options

The scalar kernels keep an explicit stack of frames in memory. With lanes,
each lane has its own.

**Option A: stacks in memory.** One small array of narrow frames per lane;
pop is a gather, push is a scatter. Simple, and a direct transcription of
the scalar kernel. Its weakness is that gathers and especially scatters are
among the slowest vector instructions (their cost also varies a great deal
between processor generations), and the popped frame is on the path to the
lane's next state, so every iteration waits for one.

**Option B: no memory stack.** For budgets up to 8 the stack has a structure
that can be exploited:

- A frame is either a *choice frame* (a link loop or a color loop with
  alternatives left) or a *single-step frame* (a CTRL frame waiting in MATCH,
  a FLOW frame waiting in SIMPLE_R) that will pop with exactly one logical
  step when control returns to it.
- Every live link loop owns one link slot and every live color loop owns one
  node's color, so at most 2N + N = 24 choice frames exist at once.
- The single-step frames hold only the `cp` or `fp` to restore: one of each
  per matched target bit. Those are the pointer trajectory, 3 bits per bit
  of target each.

So the stack can be kept in registers as a choice stack of at most 24 small
entries plus the two trajectories, and "return through k single-step frames
to the nearest choice frame" becomes arithmetic: k steps charged, `gen`
reduced by the number of CTRL frames among them, `fp` and `cp` read back
from the trajectory. This is the same reasoning as the scalar kernel's bulk
accounting, applied to unwinding. It removes all memory traffic from the
loop, at the price of a more intricate step function.

Option B is what makes the design interesting. Option A is the fallback and
the baseline to beat.

### One iteration

Two ways to define what an iteration does:

- **Micro-step:** every active lane advances by one logical step. All step
  kinds are computed every iteration and blended by the lane's phase.
- **Macro-step:** every active lane advances by one *event*: evaluate the
  next CTRL INIT and the FLOW INIT that follows it; or move a link loop to
  its next viable node, charging the certain failures in between; or unwind
  to the nearest choice frame. The logical steps of the event are subtracted
  from the lane's counter at once.

The cost model below says macro-steps are needed.

### Exactness

Each lane must end with the status, graph and logical step count the scalar
kernel would produce. The plan is to make the vector code a fast path only:

- a lane whose remaining steps fall below a margin, or that meets anything
  the vector code does not handle, is handed to the scalar kernel, which
  re-runs that job from the start with the same limits. Its result is then
  correct by construction;
- everything else is checked the way the scalar kernels were: status, graph
  and step count against the C solver for every string of small lengths and
  for random jobs.

### Lane life cycle

When a lane finishes, its result is stored and the next job loaded into it.
That is scalar work per finished job, driven by a mask of finished lanes
that is zero on almost every iteration. Lanes stay full as long as jobs
remain; near the end of a batch the leftovers go to the scalar kernel.

### Instruction sets

| | Lanes | Notes |
|---|---|---|
| AVX-512 (F, BW, DQ, VL; CD or VPOPCNTDQ for bit scans) | 8 | mask registers make the blends cheap; scatter exists if option A is used |
| AVX2 | 4 | per-lane variable shifts exist; no mask registers, no scatter |

Intel server processors and AMD Zen 4 and later have AVX-512 (Zen 4 executes
512-bit operations in two halves, so its gain over AVX2 is smaller). Intel
client processors since Alder Lake do not. Both builds would be needed, and
the scalar `auto` engine remains the answer where neither applies.

## 4. Cost model

Let `T` be the scalar cost per logical step, `C` the cost of one vector
iteration, `W` the number of lanes and `s` the average number of logical
steps a lane advances per iteration. The vector kernel costs `C / (W·s)` per
logical step and wins when `C < W·s·T`.

`T` is about 3 to 3.8 ticks for `auto` at budgets 4 to 8 (the reference
kernel's *measured* 3.6–4.6, divided by `auto`'s *measured* gain of about
1.2).

- **Micro-step, option A** (`s` = 1, `W` = 8): break-even at `C` ≈ 24–30
  ticks. An iteration that includes a gather, a scatter and the blended
  logic of every step kind is unlikely to be far below that. *Estimate:* near
  break-even. Not worth building.
- **Macro-step, option B.** One scalar search was traced with a recursive
  model of the algorithm during kernel development (target
  `001011011101111011111`, budgets 2 to 7, 502,057 logical steps; the model
  is not in the repository):

  | Event | Count |
  |---|---|
  | CTRL INIT through a decided link | 72,929 (35,673 matched) |
  | CTRL link attempt on an existing node | 39,453 |
  | CTRL link attempt on a new node | 729 |
  | FLOW link attempt on an existing node | 28,339 |
  | FLOW link attempt on a new node | 490 |

  That is about 142,000 events, 3.5 logical steps per event, from a single
  search; step 2 of section 6 is there to replace it with a proper census.
  With `s` ≈ 3.5, break-even is `C` ≈ 85–105 ticks, and a threefold
  gain over `auto` needs `C` ≈ 30 ticks per iteration. *Estimate:* six event
  kinds at roughly 20–25 vector operations each is of that order on a
  current core, provided no dependency chain runs from one iteration to the
  next through anything slow. That proviso is the whole risk.

So a plausible target is 2–3 times `auto` per core for budgets up to 8, and
the design only reaches it with both option B and macro-steps. Lanes sitting
idle or waiting in a slow path reduce it further.

## 5. Risks

- The misprediction hypothesis is unconfirmed. If mispredictions cost much
  less than estimated, the scalar kernel is closer to the ceiling than
  section 2 assumes.
- The step function for option B is intricate, and every lane has to count
  steps exactly. The scalar fallback and differential testing contain the
  correctness risk but not the effort.
- Throughput depends on the number of event kinds blended per iteration. If
  it needs many more operations than estimated, the gain disappears.
- Search lengths vary by orders of magnitude between jobs; lane refill must
  be cheap enough not to matter.
- Two code paths (AVX-512 and AVX2), on top of the scalar kernels.

## 6. What to measure first

In order, stopping when an answer makes the next step pointless:

1. **Hardware counters** on a machine that has them: branch misses, cycles
   and instructions per step for `asm` and `auto`
   (`benchmarks/characterize`). Confirms or refutes section 2.
2. **An event census** of the scalar search over the jobs a FixedRunBomb
   would actually run: logical steps per event and the mix of event kinds.
   Confirms or corrects `s`.
3. **One event kind in isolation**: the CTRL-then-FLOW advance for 8 lanes,
   in C with intrinsics, timed alone. Gives the first real number for `C`.
4. Only then a kernel, for budgets up to 4 first (smaller state), tested
   against the scalar engines on every string of small lengths.

## 7. Alternatives that may matter more

- **Cores.** Independent searches scale across cores with no design work
  beyond giving each thread its own state (the scalar kernels use static
  storage today). This multiplies whatever a single core achieves and
  should come first.
- **Enumerate and run.** For small N it is cheaper to enumerate the REVs
  than to search for each string. The number of labeled graphs with n nodes
  is 2^n · n^(2n): 5,832 for n = 3, 1,048,576 for n = 4, 312,500,000 for
  n = 5 and about 1.4·10^11 for n = 6, whatever L is; relabeling symmetry
  reduces that further. Each graph is run for L bits and marks the string it
  emits; the first budget at which a string is marked is its minimum. The
  oracle test in this repository does exactly this for n up to 5 (about 20
  seconds for n = 5, *measured*, including the solver checks). The inner loop
  is the plain REV interpreter, with no backtracking and no data-dependent
  branches, so it vectorizes trivially: W graphs per iteration. For a
  FixedRunBomb this probably settles every string that needs up to 5 nodes
  (*measured:* 1,022 of the 1,024 strings of length 10) and, with more work,
  6. The searches left over are the ones at budgets 6 to 8, which are also
  the expensive ones, and those are the lane kernel's real job.
- **Sharing work between strings.** Targets with a common prefix share the
  start of their searches. One search over the tree of targets does that
  work once; that is the LevelSearchBomb idea (`../level-search/README.md`).
  It changes the amount of search, not its speed, and combines with
  everything above.
