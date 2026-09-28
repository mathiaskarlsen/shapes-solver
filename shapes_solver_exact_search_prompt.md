# Task: Improve the exact proof search in shapes-solver

Work on the existing repository:

https://github.com/mathiaskarlsen/shapes-solver

This is a C++23 solver for a 7×9, 4-color tile-clearing puzzle.

The current solver is already good at finding strong solutions. For the difficult daily board under discussion it finds a 14-move solution in well under a second, matching the known record. However, the exact search has so far been unable to find a 13-move solution or prove that no solution of 13 moves or fewer exists. A roughly 20-minute `--target-moves 13` run did not finish.

The goal is specifically to improve the **exact proof search**. Do not focus on improving the heuristic 14-move solution finder unless doing so directly helps exact search.

The distinction is essential:

- Finding a solution of 13 moves or fewer proves that 14 was not optimal.
- Exhaustively proving that no such solution exists proves that the known 14-move solution is optimal.
- Timing out proves neither.

Never report optimality unless it has actually been established.

---

## General approach

Treat this as a sequence of controlled experiments. Do **not** implement every optimization simultaneously and then benchmark the combined result.

For each major optimization:

1. Establish a baseline.
2. Implement the optimization.
3. Run correctness tests.
4. Benchmark it independently.
5. Record the effect.
6. Keep it if useful.
7. Move on to the next experiment.

Preserve the ability to compare implementations where practical. Before substantial work, inspect the existing repository and understand the 63-bit survivor-state representation, move generation, gravity, heuristic solver, exact bounded DFS, transposition-table semantics, `--target-moves`, benchmark tooling, and tests. Do not rewrite working components unnecessarily. Use C++23 throughout.

## Primary benchmark

The key benchmark is the difficult board with `--target-moves 13`. The known solution length is 14. Answer: **Is there a solution in 13 moves or fewer?**

The exact search has three legitimate outcomes:

- **SAT / FOUND:** A solution of 13 moves or fewer exists.
- **UNSAT / PROVEN IMPOSSIBLE:** No solution of 13 moves or fewer exists.
- **UNKNOWN / TIMEOUT:** The search did not finish.

Keep these distinctions explicit in all CLI output. Retain random-board benchmarks so regressions on typical boards remain visible. Locate and preserve the actual difficult board input; if it is not in the repository, request it instead of substituting another board.

## Instrumentation first

Before changing the algorithm, improve profiling and statistics if needed. For an exact target search, report at least:

- elapsed time;
- states expanded and states expanded per second;
- moves generated and canonical duplicate children removed;
- transposition-table lookups, hits, inserts, and size;
- peak memory, if reasonably measurable;
- maximum recursion or search depth;
- lower-bound prunes;
- timeout status;
- thread count for multithreaded runs.

Make runs easy to compare. Prefer deterministic search with one thread.

## Experiment 1: Canonicalize visually identical states

The operational state identifies surviving **original tiles**:

```cpp
uint64_t alive;
```

This is useful for transitions, but it is not necessarily the best transposition-table identity. Different survivor masks can produce exactly the same settled visible board. For example, an original column `P G P` settles to a single `P` whether only its upper `P` or only its lower `P` survives. The subsequent game trees are identical, but the raw masks differ.

### Exact canonical key

Each original column has only `2^9 = 512` survivor masks. Precompute a canonical representative for every 9-bit local mask. Two masks receive the same identity **exactly when** they produce the same compacted color sequence. One implementation can choose the numerically smallest local survivor mask for each sequence.

Concatenate seven canonical 9-bit representatives into a collision-free 63-bit `uint64_t` board key. Keep the original `alive` mask as the operational state if useful, and use the visible-board key for the transposition table. This must be an exact equivalence relation, not a probabilistic hash.

Add tests showing:

- Equivalent compacted columns have identical keys.
- Visibly different columns have different keys.
- Equivalent complete boards have identical keys.
- Random states with identical keys render identically.
- Where practical, equivalent states have equivalent legal successor boards.

### Deduplicate siblings

Canonicalize every resulting child after move generation. Search a canonical child only once even if several legal moves produce it. Count duplicate children removed.

Benchmark three configurations independently:

1. Raw-state transposition table.
2. Canonical-state transposition table.
3. Canonical-state table plus sibling deduplication.

## Experiment 2: Stronger admissible lower bound

The current distinct-color bound is at most four and is weak. Implement a **provably admissible** bound using permanently separated column ranges.

For each color, mark which of the seven columns still contain it and count contiguous runs of marked columns. For example, `1101101` has three runs. Since tiles never move horizontally and empty columns never shift, same-color groups separated by a column with no tile of that color can never connect. That color needs at least one future click per run.

```text
LB(state) = runs(P) + runs(B) + runs(G) + runs(O)
```

This dominates or equals the distinct-color bound. Prune when `lower_bound(state) > moves_left`; verify the inequality against the bounded-search semantics.

For many small random states whose exact solution length can be calculated exhaustively, assert `lower_bound(state) <= actual_optimal_moves(state)`. Benchmark old and new bounds separately, reporting lower-bound prunes, expanded states, states per second, and runtime.

## Experiment 3: Optimize the transposition table

Profile the current exact solver. If `std::unordered_map<uint64_t, ...>` causes substantial runtime or memory traffic, try a compact flat/open-addressing table. The key is 64 bits and the failure-depth information likely fits in a byte or a few bits. An approximate shape is:

```cpp
struct TTEntry {
    uint64_t key;
    uint8_t proven_failed_depth;
};
```

Investigate linear or Robin Hood probing, power-of-two capacities, a sensible load factor, contiguous storage, fewer per-entry allocations, and reserving enough capacity. Correctness matters more than cleverness. Eviction can reduce pruning but must never make an exact proof unsound.

Benchmark `std::unordered_map` against the flat table for states per second, memory, hit rate, and wall time. Keep the custom implementation only if measurement justifies it.

## Experiment 4: Parallel exact proof search

Add optional multithreaded exact search, especially for the 13-move UNSAT proof. Do not share recursive mutable state unsafely. A straightforward design is:

1. Expand the first one or two levels.
2. Canonicalize and deduplicate frontier states.
3. Queue independent bounded subproblems.
4. Let worker threads search them.

For an UNSAT proof, **every** frontier task must be proven impossible. For a successful search, a valid solution in one task can stop the others. Start with per-thread transposition tables if simplest; investigate a sharded shared table only if cross-thread duplicated work merits the added complexity. Any shared table must preserve proof correctness.

Support `--threads 1`, `4`, `8`, and `16` as appropriate. Benchmark scaling, reporting wall-clock improvement **and** total states expanded. Keep single-thread mode deterministic for validation.

## Experiment 5: Breadth-first exact search

Implement a separate experimental BFS solver for the bounded question “Is empty reachable within 13 moves?” Search depths 0 through 13, using canonical visible-board keys. A state reached earlier dominates the same canonical state reached later because its future behavior is identical. Do not re-expand it at equal or greater depth.

If the empty board appears by depth 13, report SAT. If every state through depth 13 is exhausted without empty, report UNSAT. BFS may use excessive memory; that is an acceptable experimental finding. Add a memory limit or graceful failure with useful diagnostics rather than an unexplained out-of-memory crash.

Collect per-depth new states, duplicates, cumulative states, memory, and elapsed time. Compare BFS with bounded DFS on the difficult board. If clearly infeasible, document it rather than over-engineering it.

## Experiment 6: SAT-based exact proof engine

Build a second, independent bounded solver for “Does a legal sequence of at most N moves clear the board?” Use `N = 13` for the difficult board. A SAT result must decode to a replayable solution; an UNSAT result means no such solution exists.

### Integration

Prefer generating standard DIMACS CNF and invoking a mature CDCL SAT solver such as CaDiCaL or Kissat, whichever is readily available, over implementing a SAT solver. Avoid a hard-to-install permanent core dependency if DIMACS export is simpler. Possible interfaces:

```text
solver board.txt --sat-target 13
solver-sat board.txt --target 13
solver board.txt --sat-target 13 --write-cnf board13.cnf
```

### Exact encoding

Model exactly:

- tile occupancy and color at each step;
- one legal click per unfinished step;
- removal of the **entire** orthogonally connected component of the clicked color;
- legal singleton moves;
- vertical gravity and no horizontal shifting;
- an empty board by the horizon.

For “at most N,” no-op steps may be allowed only after the board is empty, or use another correct bounded-completion encoding. Do not permit removal of an arbitrary connected subset. Layered reachability from the clicked tile through same-color neighbors, with enough propagation layers to span the board, is one possible exact component encoding. Consider using original-tile survival variables because the original colors do not change. Gravity must be exact.

### Mandatory cross-validation

Generate many tiny and small boards. Answer identical bounded questions with baseline exact DFS and SAT, checking SAT/UNSAT agreement. For **every** SAT model, decode clicks and replay through the normal C++ game engine; check every move is legal and that the board clears within the bound. An unreplayable SAT model indicates an encoding or decoder bug.

If the chosen SAT solver supports a standard DRAT, LRAT, or similar independently checkable UNSAT certificate, investigate it. This is optional for the first SAT version but desirable for the 13-move instance. Document what was independently verified.

## Experiment 7: Pattern databases and exact abstractions

Investigate whether an admissible abstraction gives a materially stronger lower bound. Do not implement an abstraction with unclear admissibility. Possible relaxations include making some colors or actions free, or discarding information in a way that can only make the puzzle easier.

Any exact-search heuristic must satisfy `heuristic(state) <= true remaining optimal move count` for **every** state. An occasional overestimate invalidates an UNSAT proof.

For a promising abstraction:

1. Explain formally why it is admissible.
2. Implement it independently.
3. Brute-force check admissibility on many small states.
4. Measure bound strength and computation cost.
5. Enable it only when its pruning benefit outweighs its cost.

Acceptable outcomes include “implemented and useful,” “implemented but too expensive,” or “no convincing admissible abstraction found.” Do not force an implementation just to complete the list.

## Other safe improvements

Consider caching a lower bound in TT entries, precomputing 9-bit column properties and color-presence masks, speeding component generation, avoiding recursion-path allocations, ordering moves to find SAT earlier, canonical child deduplication, and sound dominance relations. Apply an optimization only when its correctness is understood. Proof search must remain complete.

## Preserve proof semantics

A failed call such as `can_solve(state, moves_left) == false` means the state was **exhaustively proven** impossible to clear within `moves_left` moves. A state proven impossible with 8 moves remaining is also impossible with 0 through 8, but that says nothing about 9 moves remaining.

Be especially careful when combining canonicalization, depth information, table replacement, and parallel search. Add assertions and tests where useful. A timeout or interrupted subtree must never be stored as a proven failure.

## Validation strategy

For each exact-search implementation, cross-check reduced random boards where exhaustive enumeration is cheap. Compare baseline DFS, optimized DFS, BFS, and SAT where available; all must agree on bounded SAT/UNSAT answers. Replay every returned solution through the normal game engine, which remains the source of truth for move legality.

## Benchmark methodology

Keep a benchmark-results document or README section. Record baseline versus each experiment, for example:

| Method | Time | States | States/s | Memory |
| --- | ---: | ---: | ---: | ---: |
| Baseline DFS | | | | |
| Canonical TT | | | | |
| Sibling deduplication | | | | |
| Column-run bound | | | | |
| Flat TT | | | | |
| Parallel search | | | | |
| BFS | | | | |
| SAT | | | | |

Repeat noisy timings. Use release builds and record CPU, RAM, compiler, flags, thread count, and commit hash for important runs. The primary metric is whether target 13 is conclusively answered. Secondary metrics are proof time, states expanded, throughput, memory, and general random-board performance.

## Focus

The heuristic solver already finds the known 14-move record quickly. Focus on the gap between finding one good path and proving that every path of length 13 or less fails. Do not spend substantial time tuning beam-search scores unless benchmarks show a direct proof-search benefit.

## Suggested implementation order

Follow this order unless profiling gives a compelling reason to change it; benchmark after each step:

1. Baseline instrumentation.
2. Visible-state canonicalization.
3. Sibling-child deduplication.
4. Column-run admissible bound.
5. TT profiling and flat-table experiment.
6. Multithreaded frontier search.
7. BFS experiment.
8. SAT bounded solver.
9. Admissible abstraction or pattern-database investigation.

## Final deliverable

Leave the repository in a clean state and provide a concise technical report with:

### Implementation status

For every experiment, say whether it was **kept**, **rejected**, or **experimental**, and why.

### Benchmarks

Show baseline against each major optimization.

### Exact result for the 13-move question

Report exactly one of:

1. A solution of 13 moves or fewer was found and successfully replayed.
2. No solution of 13 moves or fewer exists; the 14-move solution is proven optimal.
3. The search remains unresolved within the tested resource limits.

If SAT and DFS independently agree, mention it. If an UNSAT certificate was produced and independently checked, document verification.

### Recommended configuration

State which techniques should become the default exact solver, and list promising unfinished opportunities.

## Most important constraints

Correctness comes before speed. Do not weaken the search to make statistics look better. A 10× speed improvement that preserves rigorous UNSAT results is valuable; a faster method that silently misses legal solutions cannot answer this question. The intended result is a solver that can defensibly answer whether **this exact board** has a solution in 13 moves or fewer.
