# 7×9 tile-clearing solver

A C++23 command-line solver for a fixed 7-column, 9-row board with four colors. Click one tile to remove its entire orthogonally connected same-color component; single tiles are legal. Surviving tiles fall straight down **within their own columns**; empty columns do not move sideways. The objective is the fewest moves to clear the board.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Visual Studio multi-configuration generators the executables are in `build/Release/`; single-configuration generators put them in `build/`. A C++23-capable compiler is required.

## Local web guide

Build as above, then start the local frontend with Python 3:

```sh
python web/server.py
```

Open `http://127.0.0.1:8000/`. Select a color and click cells to paint the 7×9 board; the eraser removes individual tiles. Click **Fast solve**, then click any highlighted tile to remove that group and watch the remaining tiles fall before the next move appears. **Edit board** restores the puzzle for changes. The guide uses the fast greedy/beam solver, not optimal search, and accepts empty cells. The server binds to localhost; use `--port N` to change the port or `--solver-binary PATH` for a nonstandard build location.

## Input and CLI

A board file has exactly 9 lines of exactly 7 symbols; line 1 is the top. `P` = pink, `B` = blue, `G` = green, `O` = orange. No spaces or empty cells in the input. See `examples/initial-board.txt` for a generated example.

Put personal boards in `boards/`. Its tracked `.gitignore` preserves the directory while keeping puzzle files out of Git.

```sh
solver examples/initial-board.txt
solver examples/initial-board.txt --optimal
solver examples/initial-board.txt --fast
solver examples/initial-board.txt --show-steps
solver examples/initial-board.txt --time-limit-ms 1000
solver examples/initial-board.txt --target-moves 11 --time-limit-ms 60000
solver boards/my-board.txt --target-moves 13
solver-bench --random 1000 --seed 12345 --time-limit-ms 1000
solver-bench --random 20 --seed 12345 --fast
```

The default and `--optimal` first find a fast complete solution, then attempt to prove the minimum number of moves. With no time limit, exact search continues until proof; it can take arbitrarily long on hard inputs. `--fast` runs the greedy and beam upper-bound finders without exact DFS. `--time-limit-ms N` sets an overall deadline measured from the start of solving (0 means unlimited); greedy always completes, while beam and exact search stop at the deadline, with small overshoots for periodic checks/cleanup. A short limit can interrupt beam before it finds its improved path. The CLI prints **Optimal solution** only if a shorter solution has been ruled out, and otherwise prints **Best solution found** and **Optimality not proven**. A solution matching the distinct-color lower bound is also proven optimal mathematically, although fast mode is still labeled "Fast solution". `--show-steps` prints each settled board after the indicated move, with `.` for empty cells. Click coordinates are 1-based: column 1 is leftmost; row 1 is the **top of the currently settled board**, not the initial board. Parsing failures exit with a useful diagnostic.

`--target-moves N` (0–63) stops when it finds a complete solution in **at most N clicks**. For example, if you know a 14-click path exists, use `--target-moves 13` to seek something better. The solver first checks its greedy/beam solutions, then searches **directly at the requested bound** instead of proving every intermediate bound. It reports one of three outcomes: **Target reached** (not necessarily optimal), **No solution in N moves or fewer (proven)**, or **Time limit reached** with a replayable best solution and no claim that the target is impossible. With no `--time-limit-ms`, target search has no deadline; an unreachable bound can take a very long time to rule out. `--target-moves` cannot be combined with `--fast` or `--optimal`; `--show-steps` works with target mode.

## Algorithm

Each original tile has a permanent ID `row * 7 + column`; its color does not change. A state is a 63-bit `uint64_t` survivor mask. Gravity compacts survivors down each original column without changing their vertical order. The engine flood-fills the settled 7×9 board and returns one move per same-color component, including singletons. Transitions only clear bits, so searching cannot cycle. The engine and solver are independent of terminal output; `Solver::solve_from` can solve a partial survivor mask.

Four deterministic greedy scoring variants provide an initial complete solution. A width-10,000 beam search then keeps distinct promising states at each move depth, ranked by connected-component count, remaining tiles, and largest component. For scoring-only child states, the engine measures component sizes/colors without building removable tile masks; adjacency is precomputed and beam buffers are reused across depths. Beam search improves the incumbent but cannot prove optimality because it discards states. Exact depth-bounded DFS tries to beat that incumbent, then tries again below each new incumbent. At each bound, it orders moves using removed group size, reduction in component count, and the largest new group. The admissible lower bound is the number of colors still present (one click removes only one color). A transposition table keyed by survivor mask remembers the **largest remaining depth at which a state was proven unsolvable**; visiting the same state with no more moves left is then safely pruned. A timed-out subtree is never recorded as failed. A failed exact search below the incumbent proves that the incumbent is optimal. A complete solution remains replayable even if proof times out.

Target mode uses the same exact depth-bounded search at the requested limit. Exhausting that search proves that no path at or below the limit exists; simply finding a path at the limit does not prove optimality.

## Benchmark

`solver-bench --random N --seed S [--fast | --time-limit-ms M]` generates N independent uniform 7×9 boards from a seeded `mt19937_64` (two low bits per tile, row-major). `--fast` measures complete heuristic search without exact DFS; otherwise the default per-board time limit is 1000 ms (0 disables it). Each board reports elapsed milliseconds, **exact-search** states expanded and transposition hits (beam states are not included in these counters), maximum exact-search depth, best complete solution length, and whether optimality was proven. The summary includes median, nearest-rank p95/p99, worst-case time, and proven count. Timed-out cases are counted as **unproven**: their lengths are upper bounds, not established optima. Time includes greedy, beam, and exact search, but not board generation or printing; results depend on compiler, configuration, and machine.

### Measured results

Windows x64, Visual Studio 2026 Release (MSVC 19.51), seed 12345. Times are wall-clock measurements from `Solution::elapsed_seconds`; the 10-board runs use the first 10 of the 20 boards in the fast-mode run.

| Mode / boards | Median time | p95 time | Median moves | p95 moves | Optimal proven |
| --- | ---: | ---: | ---: | ---: | ---: |
| `--fast`, 20 boards | 737 ms | 1037 ms | 14 | 16 | 0/20 |
| `--time-limit-ms 1000`, 10 boards | 1007 ms | 1018 ms | 14 | 19 | 0/10 |
| `--time-limit-ms 3000`, 10 boards | 3090 ms | 3164 ms | 14 | 16 | 0/10 |

These are sample measurements, not performance guarantees or optimality proofs; deadline checks and cleanup can overshoot. The generated example (`examples/initial-board.txt`) clears in 12 moves (0.64 s in one Release run), and the regression test replays its full path. The distinct-color lower bound is too weak to prove optimality for these random boards at the measured limits.
