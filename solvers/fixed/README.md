# FixedSolver (planned, not implemented)

A specialized LevelSolver with the node capacity N and the target length L
fixed in advance, so that data layout and control flow can be specialized
for one (N, L).

Planned use: a neural evaluator will score the legal destinations of a link
(each existing node, or a new node) to order the moves, while an exact search
engine keeps full backtracking. The evaluator only changes the order in which
moves are tried; the search remains exhaustive, so answers stay exact.

Not part of Milestones 0 to 2.
