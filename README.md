# 7×9 tile-clearing solver

A C++23 command-line solver for a fixed 7-column, 9-row board with four colors. Click one tile to remove its entire orthogonally connected same-color component; single tiles are legal. Surviving tiles fall straight down **within their own columns**; empty columns do not move sideways. The objective is the fewest moves to clear the board.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Visual Studio multi-configuration generators the executables are in `build/Release/`; single-configuration generators put them in `build/`. A C++23-capable compiler is required.

## Input and CLI

A board file has exactly 9 lines of exactly 7 symbols; line 1 is the top. `P` = pink, `B` = blue, `G` = green, `O` = orange. No spaces or empty cells in the input. See `examples/initial-board.txt` for a generated example.

Put personal boards in `boards/`. Its tracked `.gitignore` preserves the directory while keeping puzzle files out of Git.

```sh
solver examples/initial-board.txt
solver examples/initial-board.txt --optimal
solver examples/initial-board.txt --fast
solver examples/initial-board.txt --show-steps
solver examples/initial-board.txt --time-limit-ms 1000
solver boards/today.txt --target-moves 13 --time-limit-ms 60000
solver boards/today.txt --target-moves 13 --threads 8 --time-limit-ms 60000
solver boards/today.txt --target-moves 13 --unordered-tt --distinct-color-lb
solver-bench --random 1000 --seed 12345 --time-limit-ms 1000
solver-bfs boards/today.txt --target 13 --memory-mb 256 --time-limit-ms 10000
solver-sat boards/today.txt --target 13 --write-cnf board13.cnf
solver-sat boards/today.txt --target 13 --write-cnf board13.cnf --solver minisat
```

The default and `--optimal` first find a fast complete solution, then attempt to prove the minimum number of moves. With no time limit, exact search continues until proof; it can take arbitrarily long on hard inputs. `--fast` runs the greedy and beam upper-bound finders without exact DFS. `--time-limit-ms N` sets an overall deadline measured from the start of solving (0 means unlimited); greedy always completes, while beam and exact search stop at the deadline, with small overshoots for periodic checks/cleanup. A short limit can interrupt beam before it finds its improved path. The CLI prints **Optimal solution** only if a shorter solution has been ruled out, and otherwise prints **Best solution found** and **Optimality not proven**. A solution matching the admissible column-run lower bound is proven optimal mathematically, although fast mode is still labeled "Fast solution". `--show-steps` prints each settled board after the indicated move, with `.` for empty cells. Click coordinates are 1-based: column 1 is leftmost; row 1 is the **top of the currently settled board**, not the initial board. Parsing failures exit with a useful diagnostic.

`--target-moves N` (0–63) stops when it finds a complete solution in **at most N clicks**. For example, if you know a 14-click path exists, use `--target-moves 13` to seek something better. The solver first checks its greedy/beam solutions, then searches **directly at the requested bound** instead of proving every intermediate bound. It reports one of three outcomes: **Target reached** (not necessarily optimal), **No solution in N moves or fewer (proven)**, or **Time limit reached** with a replayable best solution and no claim that the target is impossible. With no `--time-limit-ms`, target search has no deadline; an unreachable bound can take a very long time to rule out. `--target-moves` cannot be combined with `--fast` or `--optimal`; `--show-steps` works with target mode. Exact search defaults to one deterministic thread, the column-run bound, a flat failure table, and raw survivor-mask table keys. `--threads 2..16` requires target mode and distributes depth-two frontier states to workers with separate failure tables. `--canonical-tt` enables exact visible-board keys. `--distinct-color-lb` and `--unordered-tt` restore the prior lower bound and table for comparisons.

## Algorithm

Each original tile has a permanent ID `row * 7 + column`; its color does not change. A state is a 63-bit `uint64_t` survivor mask. Gravity compacts survivors down each original column without changing their vertical order. The engine flood-fills the settled 7×9 board and returns one move per same-color component, including singletons. Transitions only clear bits, so searching cannot cycle. The engine and solver are independent of terminal output; `Solver::solve_from` can solve a partial survivor mask.

Four deterministic greedy scoring variants provide an initial complete solution. A width-10,000 beam search then keeps distinct promising states at each move depth, ranked by connected-component count, remaining tiles, and largest component. For scoring-only child states, the engine measures component sizes/colors without building removable tile masks; adjacency is precomputed and beam buffers are reused across depths. Beam search improves the incumbent but cannot prove optimality because it discards states. Exact depth-bounded DFS tries to beat that incumbent, then tries again below each new incumbent. At each bound, it orders moves using removed group size, reduction in component count, and the largest new group. For each color, the admissible bound counts contiguous runs of columns containing that color, then sums over colors. Different runs of the same color can never connect: tiles and empty columns do not move horizontally. One click cannot clear two such runs. The previous distinct-color bound can be selected for comparison. The default flat open-addressed failure table stores a 63-bit survivor key and its largest **proven impossible** remaining depth without evictions; a visit with at most that depth can be pruned. Canonical visible keys optionally identify original survivor masks with identical settled color sequences. Timed-out subtrees are never stored as failures. A failed exact search below the incumbent proves the incumbent optimal. A complete solution remains replayable even if proof times out.

Target mode uses the same exact depth-bounded search at the requested limit. Exhausting that search proves that no path at or below the limit exists; simply finding a path at the limit does not prove optimality.

### Alternative bounded engines

`solver-bfs BOARD --target N [--memory-mb M] [--time-limit-ms T]` searches depths 0…N with exact canonical visible-board keys. Earlier visits dominate later visits. It prints per-depth new/duplicate/cumulative states and returns **FOUND**, **PROVEN IMPOSSIBLE**, or **UNKNOWN** when its memory/time guard interrupts it. The default memory cap is 256 MiB; peak budgeted container memory is an allocation estimate, not the process's resident set.

`solver-sat BOARD --target N --write-cnf FILE [--solver MINISAT]` exports an exact bounded DIMACS encoding, optionally invokes a MiniSat-compatible program (`solver FILE TEMP_MODEL`, exit 10/20), and replays every decoded SAT path with the normal game engine. The solver model is temporary; the requested CNF remains on disk. Occupancy/color, exactly one click on a nonempty board, complete same-color connected-component removal, and column gravity are encoded at every step. Idle steps are allowed only after the board empties. With no solver configured or if it is interrupted, the result is **UNKNOWN**, never UNSAT. `--state-hex MASK` selects a partial original-tile survivor mask for cross-validation. A Python `.py` solver adapter is also accepted: `tools/pysat_minisat_cli.py` uses optional `python-sat`/CaDiCaL 195 (`python -m pip install python-sat`); neither Python nor a SAT solver is required to build the C++ project. SAT solving has no internal deadline; impose an external process limit for hard instances. To run the additional SAT/DFS cross-validation tests, set `SHAPES_SAT_SOLVER` to a MiniSat-compatible executable or that script before `ctest`. No independent UNSAT certificate has been generated or checked.

## Benchmark

`solver-bench --random N --seed S [--fast | --time-limit-ms M]` generates N independent uniform 7×9 boards from a seeded `mt19937_64` (two low bits per tile, row-major). `--fast` measures complete heuristic search without exact DFS; otherwise the default per-board time limit is 1000 ms (0 disables it). Each board reports elapsed milliseconds, **exact-search** states expanded and transposition hits (beam states are not included in these counters), maximum exact-search depth, best complete solution length, and whether optimality was proven. The summary includes median, nearest-rank p95/p99, worst-case time, and proven count. Timed-out cases are counted as **unproven**: their lengths are upper bounds, not established optima. Time includes greedy, beam, and exact search, but not board generation or printing; results depend on compiler, configuration, and machine.

### Measured results

Windows x64, Visual Studio 2026 Release (MSVC 19.51), seed 12345. Times are wall-clock measurements from `Solution::elapsed_seconds`; the 10-board runs use the first 10 of the 20 boards in the fast-mode run.

| Mode / boards | Median time | p95 time | Median moves | p95 moves | Optimal proven |
| --- | ---: | ---: | ---: | ---: | ---: |
| `--fast`, 20 boards | 737 ms | 1037 ms | 14 | 16 | 0/20 |
| `--time-limit-ms 1000`, 10 boards | 1007 ms | 1018 ms | 14 | 19 | 0/10 |
| `--time-limit-ms 3000`, 10 boards | 3090 ms | 3164 ms | 14 | 16 | 0/10 |

These are historical sample measurements from the earlier distinct-color/unordered-map implementation, not performance guarantees or optimality proofs; deadline checks and cleanup can overshoot. The generated example (`examples/initial-board.txt`) clears in 12 moves (0.64 s in one Release run), and the regression test replays its full path.

### Exact-proof experiments on the difficult board

Input: the ignored personal board `boards/today.txt` (nine rows beginning `PPPBPGG`, ending `BGBGPOB`), **not** the generated example. Machine: AMD Ryzen 5 5600H (12 logical processors), 13.9 GiB RAM, Windows x64, Visual Studio 2026 Release / MSVC 19.51; base commit `607c24f` plus the working-tree changes in this experiment. Each DFS row below used `--target-moves 13 --time-limit-ms 10000`, except the explicitly longer run. The timeout is overall wall time; states/s divides expanded states by exact-search time (~9.1 s). Peak working set is the Windows process measurement. All DFS rows timed out; **none proves that 13 moves are impossible**. Trials are samples, not paired exhaustive completion timings.

| Experiment | Status | Expanded / 10 s | Exact states/s | Peak memory |
| --- | --- | ---: | ---: | ---: |
| Raw survivor key, unordered map, distinct colors (baseline) | comparison | 3,548,160 | 389,594 | 224 MiB |
| Canonical visible-board key, otherwise baseline | optional, slower | 3,293,184 | 361,456 | 212 MiB |
| Canonical key + sibling dedup | rejected | 3,214,336 | 352,091 | 209 MiB |
| Column-run bound, otherwise baseline | **kept** | 4,304,896 | 466,556 | 325 MiB |
| Flat table, otherwise baseline | **kept** | 4,304,896 | 471,955 | 113 MiB |
| Flat table + column runs (default, one trial) | **kept** | 5,231,616 | 573,496 | 113 MiB |
| Canonical key + flat table + column runs | optional, slower | 4,793,344 | 526,245 | 113 MiB |
| Default + 4 threads | optional | 17,328,168 | 1,900,286 | 330 MiB |
| Default + 8 threads | optional | 27,938,856 | 3,063,988 | 618 MiB |
| Default + 16 threads | optional | 32,652,328 | 3,588,061 | 600 MiB |

The single-thread default repeated at 5,117,952 states / 10 s (562,791 exact states/s, 113 MiB). Sibling dedup found **zero** duplicates in 59 million generated moves on this board; it was removed from single-thread DFS. Deduplicating the parallel depth-two frontier **across parents** removed 607 equivalent tasks. The stronger bound and flat table improve useful throughput. Sixteen workers oversubscribe this 12-logical-processor machine and do not share failure tables; their measured peak memory was similar to eight workers in this short run. An 8-thread, **60-second** run expanded 156,941,352 states, peaked at 2,453 MiB, and still timed out with a replayed 14-move solution.

Bounded BFS hit its 256 MiB allocation guard in 1.10 s while generating depth 5 (1,574,803 cumulative canonical states; 188 MiB peak budgeted allocations before a growth request was refused): **UNKNOWN**, not UNSAT. The 13-step SAT encoding exported 61,805 variables and 245,843 clauses in a 0.29 s run. A CaDiCaL 195/PySAT-backed attempt was interrupted by an external 60-second command limit without a SAT/UNSAT result. SAT and DFS agreed on 64 bounded small-state cases (12 seeded survivor masks × five horizons, plus empty and gravity-merge cases); every SAT path replayed. No UNSAT certificate was checked. A further pattern database was not enabled: a simple one-color/column relaxation reduces to the existing column-run requirement, while projecting away colors without free removal choices can change gravity/alignment and overestimate, invalidating an UNSAT proof.

On five uniform seeded random boards (`--random 5 --seed 12345 --time-limit-ms 1000`), the median exact states expanded increased from 140,288 with `--unordered-tt --distinct-color-lb` to 184,320 with the default flat table and column-run bound; all five remained unproven at this deadline. These counts measure progress under a fixed time, not finished proof times.

**Current answer for `boards/today.txt --target-moves 13`: UNKNOWN.** The known 14-move path replays; neither a 13-move solution nor an exhaustive UNSAT proof was obtained. Recommended: default single-thread flat table + column runs for deterministic runs; try `--threads 8` when memory allows, and keep `--canonical-tt` only as a comparison option on this board. Promising but unproven future work: a sound stronger abstraction, reducing cross-worker repeated subtrees, and an independently checkable SAT proof when a backend can finish.
