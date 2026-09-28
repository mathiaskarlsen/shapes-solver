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

A board file has exactly 9 lines of exactly 7 symbols; line 1 is the top. `P` = pink, `B` = blue, `G` = green, `O` = orange. No spaces or empty cells in the input. `examples/initial-board.txt` is an independently generated example, not a transcription of the reference puzzle. The original screenshots and task prompt are excluded by `.gitignore`.

```sh
solver examples/initial-board.txt
solver examples/initial-board.txt --optimal
solver examples/initial-board.txt --fast
solver examples/initial-board.txt --show-steps
solver examples/initial-board.txt --time-limit-ms 1000
solver-bench --random 1000 --seed 12345 --time-limit-ms 1000
```

The default and `--optimal` first find a fast complete solution, then attempt to prove the minimum number of moves. With no time limit, exact search continues until proof; it can take arbitrarily long on hard inputs. `--fast` runs the greedy and beam upper-bound finders without exact DFS. `--time-limit-ms N` sets an overall deadline measured from the start of solving (0 means unlimited); greedy always completes, while beam and exact search stop at the deadline, with small overshoots for periodic checks/cleanup. A short limit can interrupt beam before it finds its improved path. The CLI prints **Optimal solution** only if a shorter solution has been ruled out, and otherwise prints **Best solution found** and **Optimality not proven**. A solution matching the distinct-color lower bound is also proven optimal mathematically, although fast mode is still labeled "Fast solution". `--show-steps` prints each settled board after the indicated move, with `.` for empty cells. Click coordinates are 1-based: column 1 is leftmost; row 1 is the **top of the currently settled board**, not the initial board. Parsing failures exit with a useful diagnostic.

## Algorithm

Each original tile has a permanent ID `row * 7 + column`; its color does not change. A state is a 63-bit `uint64_t` survivor mask. Gravity compacts survivors down each original column without changing their vertical order. The engine flood-fills the settled 7×9 board and returns one move per same-color component, including singletons. Transitions only clear bits, so searching cannot cycle. The engine and solver are independent of terminal output; `Solver::solve_from` can solve a partial survivor mask.

Four deterministic greedy scoring variants provide an initial complete solution. A width-10,000 beam search then keeps distinct promising states at each move depth, ranked by connected-component count, remaining tiles, and largest component. Beam search improves the incumbent but cannot prove optimality because it discards states. Exact depth-bounded DFS tries to beat that incumbent, then tries again below each new incumbent. At each bound, it orders moves using removed group size, reduction in component count, and the largest new group. The admissible lower bound is the number of colors still present (one click removes only one color). A transposition table keyed by survivor mask remembers the **largest remaining depth at which a state was proven unsolvable**; visiting the same state with no more moves left is then safely pruned. A timed-out subtree is never recorded as failed. A failed exact search below the incumbent proves that the incumbent is optimal. A complete solution remains replayable even if proof times out.

## Benchmark

`solver-bench --random N --seed S [--time-limit-ms M]` generates N independent uniform 7×9 boards from a seeded `mt19937_64` (two low bits per tile, row-major). Default per-board time limit is 1000 ms; 0 disables it. Each board reports elapsed milliseconds, **exact-search** states expanded and transposition hits (beam states are not included in these counters), maximum exact-search depth, best complete solution length, and whether optimality was proven. The summary includes median, nearest-rank p95/p99, worst-case time, and proven count. Timed-out cases are counted as **unproven**: their lengths are upper bounds, not established optima. Time includes greedy, beam, and exact search, but not board generation or printing; results depend on compiler, configuration, and machine.

### Measured results

Windows x64, Visual Studio 2026 Release (MSVC 19.51), seed 12345. Times are wall-clock measurements from `Solution::elapsed_seconds`; the two samples below cover the **same first 10 boards**, not 20 independent boards.

| Command | Proven | Median | p95 | p99 | Worst | Median exact states expanded | Best complete solution lengths |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `--random 10 --seed 12345 --time-limit-ms 1000` | 0/10 | 1003 ms | 1013 ms | 1013 ms | 1013 ms | 0 | 12–19 moves |
| `--random 10 --seed 12345 --time-limit-ms 3000` | 0/10 | 3079 ms | 3125 ms | 3125 ms | 3125 ms | 534,528 | 12–16 moves |

At 1 second, beam search often consumes the deadline without finishing a better path; at 3 seconds, solutions improve but none of these random boards is proven optimal. The independently generated example (`examples/initial-board.txt`) clears in **12 moves** in `--fast` mode (1.07 s in one Release run); this is a found solution, not a proven optimum. The regression test replays the full path and asserts at most 12 moves. Stronger upper bounds help find good solutions; the weak admissible bound still prevents fast optimality proofs on these random boards.
