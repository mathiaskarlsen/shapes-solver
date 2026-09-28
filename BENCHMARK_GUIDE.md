# Reproducing and comparing exact-search runs

Use this guide from the repository root on `feat/exact-proof-search`. The reference code was committed as `48d217b`; record `git rev-parse HEAD` on the new computer, because later code changes can affect search order and throughput. The implementation and experiment decisions are described in [README.md](README.md).

## Reference machine and input

| Item | Current computer |
| --- | --- |
| CPU | AMD Ryzen 5 5600H, 12 logical processors |
| RAM | 14,877,257,728 bytes (13.9 GiB) |
| OS | Windows x64, version 10.0.26200 |
| Compiler/build | Visual Studio 2026 Release, MSVC 19.51, C++23 |
| Reference commit | `48d217b` (exact-search implementation); this guide adds documentation only |
| Difficult input | `boards/today.txt`, nine 7-character lines, 72 bytes, LF line endings |
| Difficult input SHA-256 | `cc8163b6f9c62c9a21360943a9f52efd8db7bbd93e72a72c836dd286938df001` |

**Back up `boards/today.txt` outside Git and copy it from the old computer to the new one before benchmarking.** The file is intentionally ignored by `boards/.gitignore` and will **not** arrive with a Git checkout. Do not substitute `examples/initial-board.txt`: its results answer a different puzzle. Copy the board as a file, preserving its bytes, and compare its SHA-256 to the value above. On Windows, the following PowerShell command works without `Get-FileHash`:

```powershell
$bytes = [IO.File]::ReadAllBytes((Resolve-Path .\boards\today.txt).Path)
([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
```

On Linux, use `sha256sum boards/today.txt`. If a transfer changed LF to CRLF, restore the original file or compare a normalized-LF hash; do not assume a different hash is the same benchmark input. The board's first line is `PPPBPGG` and its last line is `BGBGPOB`.

## Build and correctness checks

For Visual Studio / Windows PowerShell:

```powershell
git fetch origin
git switch feat/exact-proof-search
git rev-parse HEAD
cmake -S . -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
$solver = '.\build\Release\solver.exe'
$bench  = '.\build\Release\solver-bench.exe'
$bfs    = '.\build\Release\solver-bfs.exe'
$sat    = '.\build\Release\solver-sat.exe'
```

With a single-configuration generator (common on Linux), configure with `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, then build/test as above and use `./build/solver`, `./build/solver-bench`, `./build/solver-bfs`, and `./build/solver-sat`. Do not compare a Debug build against these Release measurements.

`ctest` exercises the engine, canonical keys and equivalent successors, admissible bound, raw/flat tables, bounded single/parallel DFS, BFS, and solution replay. The reference Release run passed **1/1 tests**. An external CDCL backend enables an additional **64 SAT-vs-DFS bounded questions**, including empty and gravity-merge cases; every SAT path is replayed. This dependency is optional:

```powershell
python -m pip install python-sat
$env:SHAPES_SAT_SOLVER = (Resolve-Path .\tools\pysat_minisat_cli.py).Path
ctest --test-dir build -C Release --output-on-failure
Remove-Item Env:SHAPES_SAT_SOLVER
```

Alternatively set `SHAPES_SAT_SOLVER` to a MiniSat-compatible executable. The current computer's run with a PySAT/CaDiCaL 195 backend passed **1/1 tests in 10.56 s** (the total includes all other tests). Without `SHAPES_SAT_SOLVER`, SAT cross-validation is skipped; an ordinary passing `ctest` does not mean SAT ran.

## Fixed-time DFS comparison: difficult board

Run these from the repo root in PowerShell, after defining `$solver` above. Each command targets **at most 13 moves** with a **10,000 ms total solver deadline**, including the heuristic phase. The single-thread runs are deterministic apart from where the deadline stops them. Run each configuration a few times on an idle machine and retain the raw output; cache, thermals, background tasks, memory growth, and compiler can shift throughput.

```powershell
$base = @('boards/today.txt', '--target-moves', '13', '--time-limit-ms', '10000')
& $solver @base --unordered-tt --distinct-color-lb                 # legacy baseline
& $solver @base --canonical-tt --unordered-tt --distinct-color-lb  # visible key alone
& $solver @base --unordered-tt                                      # column-run bound alone
& $solver @base --distinct-color-lb                                 # flat table alone
& $solver @base                                                      # default: flat table + column runs
& $solver @base --canonical-tt                                       # canonical key + default
& $solver @base --threads 4
& $solver @base --threads 8
& $solver @base --threads 16
```

On Linux use the same arguments with `./build/solver` in place of `& $solver`; omit the PowerShell `$base` splat and spell out `boards/today.txt --target-moves 13 --time-limit-ms 10000`. `--threads` applies only to `--target-moves` mode. Single-thread default uses raw survivor keys, the column-run lower bound, and the flat failure table. The old unordered map/distinct-color implementations remain selectable only for comparison. The single-thread **sibling dedup** experiment was rejected (zero duplicates in roughly 59 million generated moves) and its flag/code was removed: it cannot be rerun with this commit. Parallel depth-two frontier dedup *is* active; it removed 607 equivalent tasks across parents in the reference runs. No additional pattern database was implemented, so there is no command for it.

Reference observations (one run per row, except where noted). “Expanded” includes bound-pruned nodes; exact states/s divides that count by `Exact elapsed`, **not** the 10 s total limit. Windows `Peak working set` is process RSS-like memory, not table capacity. Every row below returned **time limit reached / unknown**, with a replayed 14-move incumbent:

| Configuration, in command order | Expanded | Exact states/s | Peak working set |
| --- | ---: | ---: | ---: |
| Legacy baseline | 3,548,160 | 389,594 | 224 MiB |
| Canonical key + legacy table/bound | 3,293,184 | 361,456 | 212 MiB |
| Column-run bound + unordered table | 4,304,896 | 466,556 | 325 MiB |
| Flat table + distinct-color bound | 4,304,896 | 471,955 | 113 MiB |
| Default flat table + column runs | 5,231,616 | 573,496 | 113 MiB |
| Canonical key + default | 4,793,344 | 526,245 | 113 MiB |
| Default + 4 threads | 17,328,168 | 1,900,286 | 330 MiB |
| Default + 8 threads | 27,938,856 | 3,063,988 | 618 MiB |
| Default + 16 threads | 32,652,328 | 3,588,061 | 600 MiB |

A second 10 s default run expanded **5,117,952** at **562,791 exact states/s**, **113 MiB** peak. The archived sibling-dedup experiment (not runnable with this commit) expanded **3,214,336** at **352,091 exact states/s**, **209 MiB** peak; it removed **zero** siblings. A fixed-time state count is progress under a budget, not a measured completed proof time. If a new run finds a 13-move path or finishes the impossibility proof, report the result instead of forcing a throughput comparison with a timed-out run.

For the longer resource check, run separately, without other benchmarks competing for the CPU:

```powershell
& $solver boards/today.txt --target-moves 13 --threads 8 --time-limit-ms 60000
```

Reference: **UNKNOWN** after 60.037 s total; 156,941,352 expanded, 2,653,756 exact states/s, 2,453 MiB peak, replayed 14 moves. The exact phase was 59.139 s. This DFS has **no memory cap**; table memory can keep growing on longer runs. Do not start an unlimited run without monitoring available RAM.

To inspect every settled board of a returned path, add `--show-steps` to a `solver` command. The CLI checks each click and checks that its returned path clears the board. A 14-move path is an **upper bound**, not a proof that 13 is impossible. “Target reached” with a replayed path in at most 13 moves proves a better solution; “No solution in 13 moves or fewer (proven)” plus the 14-move path proves optimality; “Time limit reached” proves neither. `solver` can exit successfully even when proof timed out—read its status text.

## Random boards and heuristic baseline

`solver-bench` uses `mt19937_64`, two low bits per tile in row-major order. Use the same seed, board count, time limit, and build configuration on both computers. It runs the exact solver unless `--fast` is specified; it does **not** support `--threads` or target-depth proofs.

```powershell
& $bench --random 5 --seed 12345 --time-limit-ms 1000
& $bench --random 5 --seed 12345 --time-limit-ms 1000 --unordered-tt --distinct-color-lb
& $bench --random 20 --seed 12345 --fast
```

Reference for the five-board exact pair: default median **184,320 expanded states** versus legacy **140,288** at a one-second *per-board* deadline; **0/5** optimality proofs in both configurations. One board spent its deadline in the heuristic phase and expanded zero exact states. Older, pre-experiment results for the optional 20-board fast run were **737 ms median**, **1,037 ms p95**, median **14 moves**, **0/20** proofs; they are historical observations, not a fresh paired run. For a larger sample use, for example, `--random 20 --seed 12345 --time-limit-ms 1000` with and without legacy flags, and record median/p95/p99, expanded states, best moves, and proven counts. Unproven move counts are upper bounds.

## BFS and SAT experiments

BFS is independently bounded by memory. The same 256 MiB allocation guard and 10 s time guard allow a direct feasibility comparison:

```powershell
& $bfs boards/today.txt --target 13 --memory-mb 256 --time-limit-ms 10000
```

Reference: **UNKNOWN (memory limit)** after **1.10 s**, partway through depth 5; **1,574,803 cumulative canonical states**, **188 MiB peak budgeted allocations** before the next growth request was refused. Budgeted allocations are not process working set. `solver-bfs` exits with code 2 for resource-limit/unknown; a **FOUND** result must include a replayable path, and **PROVEN IMPOSSIBLE** requires full exhaustion through the target.

To measure SAT **encoding only** (not solve time), choose a CNF output under `build/`:

```powershell
& $sat boards/today.txt --target 13 --write-cnf build/today13.cnf
```

Reference: export took **0.29 s**, yielding **61,805 variables** and **245,843 clauses**. The CLI says **UNKNOWN** and exits with code 2 when no solver is configured: exporting a CNF proves nothing. For small SAT/UNSAT smoke cases, first configure `python-sat` as above or supply a MiniSat-compatible executable:

```powershell
& $sat boards/today.txt --target 1 --state-hex 9 --write-cnf build/small.cnf --solver .\tools\pysat_minisat_cli.py
& $sat boards/today.txt --target 2 --state-hex 9 --write-cnf build/small.cnf --solver .\tools\pysat_minisat_cli.py
```

The reference mask `9` consists of two disconnected singletons and was **UNSAT within one move**, then **SAT in two replayed moves**. `--state-hex` uses original tile IDs and is for reduced-state checks, not a replacement for the full board. A full-board SAT attempt needs a compatible solver and **an external process time/memory limit**: `solver-sat` has no internal deadline. A CaDiCaL 195/PySAT-backed attempt at 13 moves on this computer was interrupted after an external **60 s** limit with **no SAT/UNSAT answer**. No independently checked UNSAT certificate exists. On a new machine, do not interpret process interruption or an exported CNF as UNSAT.

## Results to keep from the new computer

Record date, `git rev-parse HEAD`, SHA-256 of the transferred board, CPU/logical threads, RAM, OS, compiler/version, Release build flags, solver backend/version, exact command/flags, deadline, run count, and **each** run's status. Save raw output (for example, append `| Tee-Object -FilePath "$env:TEMP\exact-8threads.txt"` to a PowerShell solver command). For DFS copy `States explored`, `States/second (exact)`, `Exact elapsed`, `Elapsed`, `Transposition entries/hits`, `Lower-bound prunes`, `Threads`, and Windows `Peak working set`. On Linux, record process maximum RSS separately (for example `/usr/bin/time -v ./build/solver ...`); the CLI's peak-working-set line is Windows-only. For BFS record depth reached, cumulative states, reason for stopping, elapsed, and budgeted memory. For SAT record variables/clauses, backend, SAT/UNSAT/UNKNOWN, elapsed, decoded path, and any certificate verification. Keep timeouts labeled **UNKNOWN** even if a 14-move path is shown.

Copy this compact worksheet into your own notes after saving full logs; do not replace a proof result with a throughput number:

| Metric | Current computer | New computer |
| --- | --- | --- |
| Default target 13, 10 s: status / best path | UNKNOWN / 14 moves | |
| Default target 13, 10 s: exact states/s / peak RSS | 562,791 / 113 MiB (repeat run) | |
| Eight threads, 10 s: exact states/s / peak RSS | 3,063,988 / 618 MiB | |
| Eight threads, 60 s: status / expanded / peak RSS | UNKNOWN / 156,941,352 / 2,453 MiB | |
| Five random boards, 1 s each: default / legacy median expanded | 184,320 / 140,288; 0/5 proven in each | |
| BFS, 256 MiB cap: status / reached depth / cumulative states | UNKNOWN (memory) / partial 5 / 1,574,803 | |
| SAT target 13: variables / clauses / solver status | 61,805 / 245,843 / UNKNOWN after external 60 s | |

**Reference answer:** the current computer did not find a solution in 13 moves or fewer and did not prove one impossible. The target-13 question remains **UNKNOWN** under the tested limits.
