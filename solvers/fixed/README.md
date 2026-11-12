# FixedSolver(N, L) (planned, not implemented)

A specialized LevelSolver with the node capacity N and the target length L
fixed in advance, so that data layout and control flow can be specialized
for one (N, L).

Planned use: a neural evaluator will score the legal destinations of a link
(each existing node, or a new node) to order the moves, while an exact search
engine keeps full backtracking. The evaluator only changes the order in which
moves are tried; the search remains exhaustive, so answers stay exact.

Not part of Milestones 0 to 3.

## What is here instead: the Bomb-backed FixedSolver

`bomb/` holds a different solver that carries the name for now: it answers
every target of 1 to 16 bits exactly, with no search at all, by reading the
complete FixedRunBomb(16). See `bomb/README.md`. It does not use the names
or the place of the planned FixedSolver(N, L) above, which is still to be
built and would serve lengths and node counts no table covers.
