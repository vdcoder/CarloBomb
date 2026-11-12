# LevelSearchBomb (planned, not implemented)

A reusable search-frontier artifact: the exhaustive set of partial REV states
compatible with a target prefix under a node budget.

It must store two kinds of state:

- partial graphs that match so far, and
- suspended branches, where the search wanted to create a node and the
  current budget forbade it.

The second kind is what makes the artifact extendable. Resuming a search at
N+1 nodes (or more) from an N-node frontier is complete only if every branch
that was cut off by the budget can be reopened. An artifact holding only the
matching partial graphs would silently lose solutions.

Not part of Milestones 0 to 2.
