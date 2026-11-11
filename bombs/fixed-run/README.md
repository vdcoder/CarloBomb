# FixedRunBomb (planned, not implemented)

A reusable, precomputed mapping from every binary string of one fixed length
to one valid small REV witness for it.

Whether a stored witness has a certified minimum node count depends on how
the artifact was built, and the artifact must record which it is: witnesses
produced by MinimalSolver are certified minima; witnesses produced by a
budgeted or guided search are only known to be valid.

Not part of Milestone 0.
