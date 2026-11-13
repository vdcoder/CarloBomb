# Colorless REVs: the experiment

Research code for the question raised in PR 8: can a compact Bomb store
uncolored, canonically numbered topologies and find the colors again from
the string? The findings are in [REPORT.md](REPORT.md). What was built on
them, the candidates for v4 as files and their measurements, is in
[../v4](../v4/README.md).

This directory defines no format. The tools open a complete fixed-width
Bomb for reading, validate it with the ordinary reader, and keep everything
else in memory; nothing is written to a Bomb, a journal or a work
directory, and no existing source file was changed for it.

| File | What it is |
|---|---|
| `colorless.h`, `colorless.c` | Topology and colors apart; canonical numbering; recoloring and its brute-force reference; the numbering of solver witnesses; links in base N; the rank of a canonical topology |
| `test_colorless.c` | Tests of the above (`ctest -R test_colorless`): 3.8 million checks |
| `colorless_scan.c` | Every REV of a Bomb: reachability, numbering, canonical topologies and how many strings share one, all colorings and what they predict, search steps |
| `colorless_bench.c` | Time of recoloring against verification, REV by REV, with percentiles |
| `colorless_lookup.c` | Candidate layouts built in memory, checked to return every stored REV exactly, and timed end to end |
| `colorless_cold.c` | Lookups with the files out of the page cache |
| `storage.py` | Sizes of the combinations, from the counts the scan wrote |
| `smoke.cmake` | The tools on a 13-bit Bomb built for the occasion (`ctest -R colorless_smoke`) |
| `results/` | The output of every run the report quotes |

## Running it

Build as usual; the experiment is part of the default build with GCC or
Clang on a 64-bit machine (`-DCARLOBOMB_EXPERIMENTS=OFF` leaves it out).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

`BOMB` is a complete fixed-width Bomb, for example
`~/frb24/fixed-run-24.frb`. All of these only read it. They are safe beside
a running ladder as far as files go, but they take a processor and up to
1.5 GB of memory for a few minutes, and the timings want a quiet machine.

```bash
build/colorless_scan --bomb BOMB --brute --data scan-24.data > scan-24.txt
```

About 2 minutes at 24 bits on one core (80 to 100 seconds without `--brute`),
1.5 GB of memory at its peak for the set of topologies.

```bash
build/colorless_bench --bomb BOMB > bench-24.txt
```

40 seconds. Run it on a quiet machine: it times single calls.

```bash
build/colorless_lookup --bomb BOMB > lookup-24.txt
```

3 to 4 minutes, 0.4 GB of memory beside the mapped Bomb: it builds five
layouts and looks every string up in each of them.

```bash
python3 experiments/colorless/storage.py scan-24.data
```

```bash
build/carlobomb bomb compact --file BOMB --output COMPACT
```

```bash
build/colorless_cold --fixed BOMB --compact COMPACT
```

The last two are for the cold reads only: they need the compact file of
PR 8 beside the fixed-width one (`COMPACT`: a new file of your choosing, 218
MB at 24 bits; the source is not touched).

The counts are the same on every machine for the same Bomb: compare the
table CRC-32 in the first line of each output (`dfd98e11` for the 24-bit
Bomb). The times are of the machine they were taken on.

## What the names in the outputs mean

- **Topology**: the links of a REV without its colors, ports 0 and 1 kept
  apart, root kept.
- **Canonical**: numbered breadth-first from the root, port 0 before port 1.
- **Solver's numbering**: nodes in the order the machine first reaches them
  while emitting the string, which is how the LevelSolver creates them.
- **Recoloring**: finding colors for a topology with which it emits a given
  string, by running the machine and letting the string decide.
- **Rank**: the place of a canonical topology among all canonical topologies
  of its node count (1, 12, 216, 5,248, 160,675, 5,931,540, 256,182,290,
  12,665,445,248, 705,068,085,303 of them for 1 to 9 nodes).
- **A**: one bit for every string of fewer than L bits, saying which of its
  two extensions holds its answer. **B**: the node count of every string of
  L bits in 4 bits, with counts at checkpoints, from which the place of its
  REV among those of its node count is counted.
