#pragma once

#include "game.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tiles {

enum class BfsStatus { FOUND, UNSAT, RESOURCE_LIMIT };
enum class BfsLimit { none, memory, time };

struct BfsDepthStats {
    int depth = 0;
    std::uint64_t new_states = 0;
    std::uint64_t duplicate_states = 0;
    std::uint64_t cumulative_states = 0;
};

struct BfsOptions {
    // Conservative accounting for the node array, hash entries and hash buckets.
    // Zero time limit means unlimited time; memory is always bounded.
    std::size_t memory_limit_bytes = 256ULL * 1024 * 1024;
    std::chrono::milliseconds time_limit{0};
};

struct BfsResult {
    BfsStatus status = BfsStatus::RESOURCE_LIMIT;
    BfsLimit limit = BfsLimit::none;
    // Original-tile masks, replayable from the supplied initial state.
    std::vector<Move> moves;
    // Depth zero contains the initial board. Child counts belong to their depth.
    // The final row may be partial when FOUND or RESOURCE_LIMIT interrupts a layer.
    std::vector<BfsDepthStats> depths;
    double elapsed_seconds = 0;
    std::size_t memory_bytes = 0; // Conservative peak container allocation estimate.
    std::uint64_t states_expanded = 0;
};

// Exhaustive shortest-path search for a solution using at most target_moves moves.
// Only completed exhaustion through the bound returns UNSAT. Invalid bounds/states/
// resource options throw std::invalid_argument or std::out_of_range.
BfsResult bounded_bfs(const Game& game, State initial, int target_moves,
                      BfsOptions options = {});

} // namespace tiles
