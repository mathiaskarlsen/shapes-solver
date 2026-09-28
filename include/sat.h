#pragma once

#include "game.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tiles {

// This is an at-most-horizon proof, not an optimality claim. An absent solver
// yields unknown after exporting the CNF; only a completed UNSAT run proves
// impossibility. DIMACS remains on disk; the decoded solver model is temporary.
enum class SatStatus { found, proven_impossible, resource_limit, unknown };

struct SatOptions {
    std::string cnf_path;
    std::string solver_path; // MiniSat CLI: solver <cnf_path> <temporary_model_path>
    std::uint64_t max_variables = 150'000;
    std::uint64_t max_clauses = 1'500'000;
};

struct SatResult {
    SatStatus status = SatStatus::unknown;
    std::vector<Move> moves;
    std::uint64_t variables = 0;
    std::uint64_t clauses = 0;
    std::string detail;
};

// initial consists of original tile IDs. target_moves >= 0; at most 63 moves.
// cnf_path must name an output file; no external solver is used if solver_path
// is empty. A SAT model is replayed against Game rather than trusted blindly.
SatResult solve_sat_bounded(const Game& game, State initial, int target_moves,
                            const SatOptions& options = {});

} // namespace tiles
