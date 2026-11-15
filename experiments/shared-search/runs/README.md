# Logs of the runs

What `shared_search`, `nodes_digest`, `journal_digest` and `carlobomb bomb
verify` printed, on 14 November 2026 in the development container (4
virtual CPUs, Intel Xeon under KVM, 15.7 GB, GCC 13.3 `-O3`). The first
line of a log is the command; the last, its exit code and time. Paths are
shortened: `LADDER` is the ladder of 16 to 24 bits built in the container,
`LADDER-WITH-RULE` the one of 16 to 21 bits built there with the suffix
rule of pull request #10, `RUNS` the scratch directory the runs wrote to. `y25.frb` and
`y26.frb` there are the 25- and 26-bit tables the experiment wrote.

`../FULL_RUNS.md` and `../PROPOSAL.md` say what the numbers mean.

| Files | What |
|---|---|
| `n25.txt`, `n26.txt` | every string of 25 and of 26 bits, from nothing |
| `c25.txt`, `c26.txt` | the same as the ladder's level, on the Bomb below, with the ladder's searches counted, the certified totals and table checksums to meet |
| `b25-every-stretch.txt` | 25 bits as a level, each string begun at the bound of every stretch: the same table |
| `sha256.txt` | the files written, and their compact files |
| `verify.txt` | `bomb verify` on the two tables, each against the Bomb one bit shorter |
| `recertify.txt` | strings of the two tables solved again from one node up by the production solver |
| `compact.txt` | `bomb compact` of the two tables, `nodes_digest` on both formats, `journal_digest` on a journal |
| `j17.txt` to `j24.txt` | each level of `LADDER` against its journal, search for search, and its table |
| `k17.txt` to `k21.txt` | the same for `LADDER-WITH-RULE`, with a build that could read its work directories |
| `jd17.txt` to `jd24.txt` | `journal_digest` of the journals of `LADDER` |
| `digests.txt` | `nodes_digest` of the Bombs of `LADDER` |
| `v24.txt` | 24 bits as a level, compared with the ladder's table in memory, REV for REV |
| `p22-1.txt` to `p22-16.txt` | 22 bits as a level in 1 to 32,768 classes (`--prefix-bits` 1 to 16) |
| `p24-6.txt`, `p24-8.txt`, `p26-6.txt` | 24 bits in 32 and in 128 classes, 26 bits in 32 (512 classes: `j24.txt`, `c26.txt`) |
| `t22-*.txt` | one process, with and without counting the ladder's searches |
| `d22-3.txt` | three processes |
| `e26-*.txt`, `e26-*.jobs` | one class of the 26-bit level run by itself; the `.jobs` file is its passes: class, budget, wanted, found, steps |
