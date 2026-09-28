#pragma once

#include "game.h"

#include <chrono>
#include <cstdint>
#include <vector>

namespace tiles {

struct Solution {
    std::vector<Move> moves;
    int fast_length = 0;
    bool optimal = false;
    std::uint64_t states_expanded = 0;
    std::uint64_t transposition_hits = 0;
    int max_search_depth = 0;
    double elapsed_seconds = 0;
};

class Solver {
public:
    explicit Solver(const Game& game) : game_(game) {}
    // A zero time limit means no limit. Fast mode never runs the exact search.
    Solution solve(bool prove_optimal = true,
                   std::chrono::milliseconds time_limit = std::chrono::milliseconds{0}) const;
    // Solve a reachable subset of original tiles (useful for partial boards).
    Solution solve_from(State initial, bool prove_optimal = true,
                        std::chrono::milliseconds time_limit = std::chrono::milliseconds{0}) const;

private:
    const Game& game_;
};

} // namespace tiles
