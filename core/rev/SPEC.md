# REV execution model

This is the normative definition of a Runnable Existential Volume (REV) for
CarloBomb. `rev.c` implements it; every solver result is checked against it.

**Source of authority.** The operational reference is the historical solver,
`solvers/level/fastest/c/legacy_original.c`. No other REV specification
existed in the repository when this was written, so there was nothing to
reconcile. If one appears, differences must be resolved here explicitly.

## Graph

A REV has `N >= 1` nodes, numbered `0 .. N-1`. Node 0 is the root.

Each node has

- a **color**: `WHITE = 0` or `BLACK = 1`;
- two directed **links**, `link[0]` and `link[1]`, each the index of a node in
  the same graph. Self-links and repeated targets are allowed.

`RevGraph` in `rev.h` can hold up to `REV_MAX_NODES` (64) nodes. That is a
storage limit of this implementation, not part of the model.

A graph is **executable** when every color is 0 or 1 and every link is in
`0 .. N-1`. `rev_graph_validate` checks exactly this. The value
`UNDECIDED = -1` may appear in a field only inside a solver while a graph is
being synthesized; it is never valid in an executable graph.

## Execution

The machine has two node positions, `fp` and `cp`, both starting at the root,
and alternates two operations:

**CTRL**

1. read the color of node `fp`;
2. use it to select a link of node `cp`;
3. move `cp` along that link;
4. emit the color of the new `cp` node.

**FLOW**

1. read the color of node `cp`;
2. use it to select a link of node `fp`;
3. move `fp` along that link;
4. emit nothing.

The order is CTRL, FLOW, CTRL, FLOW, ... The first bit is emitted by the first
CTRL.

```
fp = 0
cp = 0

repeat until L bits emitted:
    d = color[fp]
    cp = link[cp][d]
    emit color[cp]

    if L bits have been emitted:
        STOP

    d = color[cp]
    fp = link[fp][d]
```

The two operations are not symmetric and the two positions are not
interchangeable: each pointer is steered by the color under the *other* one,
and only `cp` produces output. This is not a walk of a single pointer over the
graph. `tests/test_rev.c` contains a graph whose correct output is
`100100100...` and whose single-pointer walk would be `111...`.

Execution is deterministic and never halts on its own; the output is an
infinite binary sequence, eventually periodic because the state `(fp, cp)`
takes at most `N^2` values.

## Matching a target

A target is a nonempty string of the characters `0` and `1`.

A REV **matches** target `S` when its first `len(S)` output bits equal `S`
exactly. Bits after the prefix are unconstrained.

Stopping after any number of bits does not change the bits already emitted.
The interpreter (`rev_machine_next_bit`) performs the FLOW that precedes bit
`k >= 1` lazily, just before the CTRL that emits it. Since FLOW emits nothing,
this is the same machine as the pseudocode above.

## Synthesis conventions used by the baseline solver

These describe how the historical solver builds graphs. They are properties of
that solver, recorded here so results can be compared; they do not restrict
what a REV is.

- **Demand-driven numbering.** Search starts with node 0 alone. A new node
  always receives the next unused index. An unresolved link may point to any
  already-created node, or create the next node if the budget allows. There is
  no forward-link or other structural restriction.
- **Move order.** An undecided color is tried as WHITE, then BLACK. An
  undecided link is tried against existing nodes `0, 1, 2, ...`, then a new
  node. The first complete solution in this order is returned.
- **Automatic coloring.** When CTRL lands on an uncolored node, the node takes
  the color the target requires at that position.
- **Consequence for node count.** Because WHITE-then-BLACK is exhausted for an
  existing link target before a new node is tried only *within* one choice
  point, a search with a generous budget can return more nodes than necessary.
  Example: target `1` with budget 2 yields a 2-node graph although 1 node
  suffices. Only the MinimalSolver, which excludes each smaller budget first,
  certifies a minimum.
- **Completion.** A solution may leave links the prefix never traversed
  undecided. Before returning, LevelSolver sets such links to node 0 (and any
  undecided color to WHITE, though the search never leaves one). These fields
  cannot affect the matched prefix; they do determine the output after it.

## Text format

```
REV 1
nodes 3
0 B 2 1
1 W 0 0
2 W 1 2
```

- Line 1: `REV 1` (format version).
- Line 2: `nodes N`.
- Then N lines `index color link0 link1`, in index order. Color is `W` or `B`
  (`0` and `1` are also accepted on input).
- Blank lines are ignored; `#` starts a comment that runs to end of line.

`rev_graph_read` rejects anything else, and validates the graph it returns.
The example is the graph the historical solver prints for `010010001`.
