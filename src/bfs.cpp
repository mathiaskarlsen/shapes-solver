#include "bfs.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tiles {
namespace {

// Charge the actual requested container allocation and a small allowance per
// allocation for allocator bookkeeping. Rehashing and vector growth are charged
// before allocation, including the interval where both old and new storage live.
struct MemoryBudget {
    static constexpr std::size_t kAllocationOverhead = 32;
    std::size_t limit;
    std::size_t used = 0;
    std::size_t peak = 0;

    void charge(std::size_t bytes) {
        if (bytes > limit - used) throw std::bad_alloc{};
        used += bytes;
        peak = std::max(peak, used);
    }

    void release(std::size_t bytes) noexcept { used -= bytes; }
};

template<class T>
struct BudgetAllocator {
    using value_type = T;
    MemoryBudget* budget;

    explicit BudgetAllocator(MemoryBudget& value) noexcept : budget(&value) {}
    template<class U>
    BudgetAllocator(const BudgetAllocator<U>& other) noexcept : budget(other.budget) {}

    T* allocate(std::size_t n) {
        if (n > (std::numeric_limits<std::size_t>::max() - MemoryBudget::kAllocationOverhead) /
                    sizeof(T)) {
            throw std::bad_alloc{};
        }
        const auto bytes = n * sizeof(T) + MemoryBudget::kAllocationOverhead;
        budget->charge(bytes);
        try {
            return std::allocator<T>{}.allocate(n);
        } catch (...) {
            budget->release(bytes);
            throw;
        }
    }

    void deallocate(T* p, std::size_t n) noexcept {
        std::allocator<T>{}.deallocate(p, n);
        budget->release(n * sizeof(T) + MemoryBudget::kAllocationOverhead);
    }

    template<class U>
    bool operator==(const BudgetAllocator<U>& other) const noexcept {
        return budget == other.budget;
    }
};

struct Node {
    State state;
    std::size_t parent;
    Move move;
};

using Nodes = std::vector<Node, BudgetAllocator<Node>>;
using Seen = std::unordered_set<State, std::hash<State>, std::equal_to<State>,
                                BudgetAllocator<State>>;

} // namespace

BfsResult bounded_bfs(const Game& game, State initial, int target_moves, BfsOptions options) {
    if (target_moves < 0 || target_moves > kColumns * kRows) {
        throw std::out_of_range("BFS target must be between 0 and 63");
    }
    if ((initial & ~kFullBoard) != 0 || options.time_limit.count() < 0 ||
        options.memory_limit_bytes == 0) {
        throw std::invalid_argument("invalid BFS initial state or resource limit");
    }

    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    BfsResult result;
    MemoryBudget budget{options.memory_limit_bytes};
    const auto finish = [&](BfsStatus status) {
        result.status = status;
        result.elapsed_seconds = std::chrono::duration<double>(Clock::now() - started).count();
        result.memory_bytes = budget.peak;
        return std::move(result);
    };

    try {
        // These two public-result arrays have bounded sizes. Reserve the layer
        // statistics now and account for their capacity in the same budget.
        const auto stats_bytes = (static_cast<std::size_t>(target_moves) + 1) *
                                 sizeof(BfsDepthStats) + MemoryBudget::kAllocationOverhead;
        budget.charge(stats_bytes);
        result.depths.reserve(static_cast<std::size_t>(target_moves) + 1);
        result.depths.push_back({0, 1, 0, 1});
        if (game.is_empty(initial)) return finish(BfsStatus::FOUND);
        if (target_moves == 0) return finish(BfsStatus::UNSAT);

        Nodes nodes{BudgetAllocator<Node>{budget}};
        Seen seen{0, std::hash<State>{}, std::equal_to<State>{},
                  BudgetAllocator<State>{budget}};
        nodes.push_back({initial, 0, {}});
        seen.insert(game.canonical_key(initial));

        std::size_t level_start = 0;
        std::size_t level_end = 1;
        for (int depth = 0; depth < target_moves && level_start < level_end; ++depth) {
            result.depths.push_back({depth + 1, 0, 0, result.depths.back().cumulative_states});
            auto& row = result.depths.back();
            for (std::size_t index = level_start; index < level_end; ++index) {
                // Clock calls are deliberately amortized across expansions.
                if ((index == level_start || (result.states_expanded & 1023) == 0) &&
                    options.time_limit.count() > 0 &&
                    Clock::now() - started >= options.time_limit) {
                    result.limit = BfsLimit::time;
                    return finish(BfsStatus::RESOURCE_LIMIT);
                }
                ++result.states_expanded;
                const State state = nodes[index].state;
                const auto moves = game.generate_moves(state);
                for (const Move& move : moves) {
                    const State child = game.apply_move(state, move);
                    if (game.is_empty(child)) {
                        // A goal seen in this layer is necessarily shortest.
                        ++row.new_states;
                        ++row.cumulative_states;
                        const std::size_t length = static_cast<std::size_t>(depth + 1);
                        const auto path_bytes = length * sizeof(Move) +
                                                MemoryBudget::kAllocationOverhead;
                        budget.charge(path_bytes);
                        result.moves.reserve(length);
                        result.moves.push_back(move);
                        for (std::size_t at = index; at != 0; at = nodes[at].parent) {
                            result.moves.push_back(nodes[at].move);
                        }
                        std::reverse(result.moves.begin(), result.moves.end());
                        return finish(BfsStatus::FOUND);
                    }
                    const State key = game.canonical_key(child);
                    if (seen.contains(key)) {
                        ++row.duplicate_states;
                        continue;
                    }
                    // Keep the original survivor mask: a canonical key is only
                    // an identity for deduplication, not a transition state.
                    nodes.push_back({child, index, move});
                    seen.insert(key);
                    ++row.new_states;
                    ++row.cumulative_states;
                }
            }
            level_start = level_end;
            level_end = nodes.size();
        }
        return finish(BfsStatus::UNSAT);
    } catch (const std::bad_alloc&) {
        result.moves.clear();
        result.limit = BfsLimit::memory;
        return finish(BfsStatus::RESOURCE_LIMIT);
    }
}

} // namespace tiles
