#include "solver.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tiles {
namespace {

int color_bit(char color) {
    switch (color) {
    case 'P': return 1;
    case 'B': return 2;
    case 'G': return 4;
    default: return 8;
    }
}

int color_lower_bound(const MoveList& moves) {
    int colors = 0;
    for (const Move& move : moves) colors |= color_bit(move.color);
    return std::popcount(static_cast<unsigned>(colors));
}

struct RankedMove {
    Move move;
    int score;
};

std::vector<Move> greedy(const Game& game, State initial, int variant) {
    std::vector<Move> path;
    path.reserve(kColumns * kRows);
    State state = initial;
    while (state) {
        const MoveList moves = game.generate_moves(state);
        int best_score = -100000;
        Move best;
        for (const Move& move : moves) {
            const BoardMetrics next = game.measure(game.apply_move(state, move));
            const int merges = moves.count - next.components - 1;
            int score = 0;
            switch (variant) {
            case 0: score = move.size * 100 + merges * 15; break;
            case 1: score = move.size * 15 + merges * 100; break;
            case 2: score = move.size * 30 + merges * 40 + next.largest * 5; break;
            default: score = move.size * 70 + merges * 35 + next.largest * 3; break;
            }
            if (score > best_score) {
                best_score = score;
                best = move;
            }
        }
        path.push_back(best);
        state = game.apply_move(state, best);
    }
    return path;
}

struct BeamNode {
    State state;
    int parent;
    Move move;
};

struct BeamCandidate {
    State state;
    int parent;
    Move move;
    int score;
};

// Beam search supplies an upper bound only. Dropping states is never used as
// evidence of optimality; the depth-bounded DFS below provides that proof.
std::vector<Move> beam(const Game& game, State initial, int upper_bound,
                       std::chrono::steady_clock::time_point deadline, bool limited) {
    constexpr int width = 10000;
    std::vector<BeamNode> nodes{{initial, -1, {}}};
    std::vector<int> frontier{0};
    std::vector<BeamCandidate> candidates;
    std::unordered_set<State> seen;
    for (int depth = 1; depth < upper_bound && !frontier.empty(); ++depth) {
        if (limited && std::chrono::steady_clock::now() >= deadline) break;
        candidates.clear();
        seen.clear();
        candidates.reserve(frontier.size() * 16);
        seen.reserve(frontier.size() * 32);
        int checked = 0;
        for (int index : frontier) {
            if (limited && (++checked & 127) == 0 &&
                std::chrono::steady_clock::now() >= deadline) return {};
            const State state = nodes[index].state;
            for (const Move& move : game.generate_moves(state)) {
                const State child = game.apply_move(state, move);
                if (!child) {
                    std::vector<Move> path{move};
                    for (int parent = index; parent != 0; parent = nodes[parent].parent) {
                        path.push_back(nodes[parent].move);
                    }
                    std::reverse(path.begin(), path.end());
                    return path;
                }
                if (!seen.insert(child).second) continue;
                const BoardMetrics groups = game.measure(child);
                if (depth + groups.distinct_colors >= upper_bound) continue;
                const int tiles = std::popcount(child);
                const int score = 110 * groups.components + 2 * tiles - 5 * groups.largest;
                candidates.push_back({child, index, move, score});
            }
        }
        const auto better = [](const BeamCandidate& a, const BeamCandidate& b) {
            if (a.score != b.score) return a.score < b.score;
            return a.state < b.state;
        };
        if (candidates.size() > width) {
            std::nth_element(candidates.begin(), candidates.begin() + width,
                             candidates.end(), better);
            candidates.resize(width);
        }
        std::sort(candidates.begin(), candidates.end(), better);
        frontier.clear();
        frontier.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            frontier.push_back(static_cast<int>(nodes.size()));
            nodes.push_back({candidate.state, candidate.parent, candidate.move});
        }
    }
    return {};
}

// Failure-only table: key zero is reserved for an empty slot. DFS never stores
// empty boards, and a nonempty visible board has a nonzero canonical key.
class FlatFailures {
public:
    void reserve(std::size_t entries) {
        if (entries < keys_.size() - keys_.size() / 4) return;
        std::size_t capacity = std::max<std::size_t>(keys_.size(), 16);
        while (entries >= capacity - capacity / 4) capacity *= 2;
        rehash(capacity);
    }

    bool proven(State key, unsigned char remaining) const {
        if (keys_.empty()) return false;
        std::size_t slot = bucket(key);
        while (keys_[slot]) {
            if (keys_[slot] == key) return depths_[slot] >= remaining;
            slot = (slot + 1) & mask_;
        }
        return false;
    }

    bool record(State key, unsigned char remaining) {
        reserve(count_ + 1);
        std::size_t slot = bucket(key);
        while (keys_[slot]) {
            if (keys_[slot] == key) {
                depths_[slot] = std::max(depths_[slot], remaining);
                return false;
            }
            slot = (slot + 1) & mask_;
        }
        keys_[slot] = key;
        depths_[slot] = remaining;
        ++count_;
        return true;
    }

    std::size_t size() const { return count_; }

private:
    std::size_t bucket(State key) const {
        return static_cast<std::size_t>((key * 0x9e3779b97f4a7c15ULL) >> shift_);
    }

    void rehash(std::size_t capacity) {
        std::vector<State> old_keys = std::move(keys_);
        std::vector<unsigned char> old_depths = std::move(depths_);
        keys_.assign(capacity, 0);
        depths_.assign(capacity, 0);
        mask_ = capacity - 1;
        shift_ = 64 - std::bit_width(mask_);
        for (std::size_t i = 0; i < old_keys.size(); ++i) {
            if (!old_keys[i]) continue;
            std::size_t slot = bucket(old_keys[i]);
            while (keys_[slot]) slot = (slot + 1) & mask_;
            keys_[slot] = old_keys[i];
            depths_[slot] = old_depths[i];
        }
    }

    std::vector<State> keys_;
    std::vector<unsigned char> depths_;
    std::size_t count_ = 0;
    std::size_t mask_ = 0;
    unsigned shift_ = 0;
};

struct Search {
    enum class Result { failed, found, timed_out };
    const Game& game;
    Solution& solution;
    std::chrono::steady_clock::time_point deadline;
    bool limited;
    SearchOptions options;
    std::unordered_map<State, unsigned char> failed;
    FlatFailures flat;
    std::array<Move, kColumns * kRows> path{};
    int found_depth = 0;
    std::atomic<bool>* stop = nullptr;

    Result dfs(State state, int left, int depth) {
        if (!state) {
            found_depth = depth;
            return Result::found;
        }
        if (left == 0) return Result::failed;
        // Checking periodically avoids a clock call per recursive node.
        if ((solution.states_expanded & 1023) == 0) {
            if (stop && stop->load(std::memory_order_relaxed)) return Result::timed_out;
            if (limited && std::chrono::steady_clock::now() >= deadline) return Result::timed_out;
        }
        const State key = options.canonical_keys ? game.canonical_key(state) : state;
        ++solution.transposition_lookups;
        if (proven_failure(key, left)) {
            ++solution.transposition_hits;
            return Result::failed;
        }
        ++solution.states_expanded;
        solution.max_search_depth = std::max(solution.max_search_depth, depth);
        const MoveList moves = game.generate_moves(state);
        solution.moves_generated += moves.count;
        if ((options.column_run_bound ? game.column_run_lower_bound(state) :
                                         color_lower_bound(moves)) > left) {
            ++solution.lower_bound_prunes;
            record_failure(key, left);
            return Result::failed;
        }
        std::array<RankedMove, kColumns * kRows> ordered{};
        for (int i = 0; i < moves.count; ++i) {
            const Move move = moves.moves[i];
            const BoardMetrics next = game.measure(game.apply_move(state, move));
            ordered[i] = {move, move.size * 16 +
                        (moves.count - next.components - 1) * 24 + next.largest * 2};
        }
        std::sort(ordered.begin(), ordered.begin() + moves.count,
                  [](const RankedMove& a, const RankedMove& b) {
                      return a.score > b.score;
                  });
        for (int i = 0; i < moves.count; ++i) {
            path[depth] = ordered[i].move;
            const Result result = dfs(game.apply_move(state, path[depth]), left - 1, depth + 1);
            if (result != Result::failed) return result;
        }
        record_failure(key, left);
        return Result::failed;
    }

    bool proven_failure(State key, int left) const {
        if (options.flat_table) return flat.proven(key, static_cast<unsigned char>(left));
        const auto it = failed.find(key);
        return it != failed.end() && it->second >= left;
    }

    std::size_t table_size() const { return options.flat_table ? flat.size() : failed.size(); }

    void reserve_table(std::size_t entries) {
        if (options.flat_table) flat.reserve(entries);
        else failed.reserve(entries);
    }

    void record_failure(State key, int left) {
        if (options.flat_table) {
            if (flat.record(key, static_cast<unsigned char>(left)))
                ++solution.transposition_inserts;
        } else {
            auto [it, inserted] = failed.try_emplace(key, static_cast<unsigned char>(left));
            if (inserted) ++solution.transposition_inserts;
            if (!inserted && it->second < left) it->second = static_cast<unsigned char>(left);
        }
    }
};

struct FrontierTask {
    State state;
    Move first;
    Move second;
};

Search::Result search_parallel(const Game& game, State initial, int target,
                               std::chrono::steady_clock::time_point deadline, bool limited,
                               SearchOptions options, Solution& solution) {
    const MoveList first_moves = game.generate_moves(initial);
    ++solution.states_expanded;
    solution.moves_generated += first_moves.count;
    std::vector<FrontierTask> frontier;
    std::unordered_set<State> seen;
    for (const Move& first : first_moves) {
        const State after_first = game.apply_move(initial, first);
        if (!after_first) {
            solution.moves = {first};
            return Search::Result::found;
        }
        const MoveList second_moves = game.generate_moves(after_first);
        ++solution.states_expanded;
        solution.moves_generated += second_moves.count;
        for (const Move& second : second_moves) {
            const State child = game.apply_move(after_first, second);
            if (!child) {
                solution.moves = {first, second};
                return Search::Result::found;
            }
            if (!seen.insert(game.canonical_key(child)).second) {
                ++solution.canonical_duplicate_children;
                continue;
            }
            frontier.push_back({child, first, second});
        }
    }
    if (limited && std::chrono::steady_clock::now() >= deadline)
        return Search::Result::timed_out;
    solution.max_search_depth = 2;
    std::atomic<std::size_t> next{0};
    std::atomic<bool> stop{false};
    std::mutex winner_mutex;
    std::vector<Move> winning_path;
    std::vector<Solution> worker_stats(options.threads);
    std::vector<std::size_t> worker_entries(options.threads);
    std::vector<std::thread> workers;
    workers.reserve(options.threads);
    for (unsigned index = 0; index < options.threads; ++index) {
        workers.emplace_back([&, index] {
            Solution& stats = worker_stats[index];
            Search search{game, stats, deadline, limited, options};
            search.stop = &stop;
            search.reserve_table(65536);
            while (!stop.load(std::memory_order_relaxed)) {
                const std::size_t task = next.fetch_add(1, std::memory_order_relaxed);
                if (task >= frontier.size()) break;
                const FrontierTask& branch = frontier[task];
                search.path[0] = branch.first;
                search.path[1] = branch.second;
                const auto outcome = search.dfs(branch.state, target - 2, 2);
                if (outcome == Search::Result::found) {
                    {
                        std::lock_guard lock(winner_mutex);
                        if (winning_path.empty()) {
                            winning_path.assign(search.path.begin(),
                                                search.path.begin() + search.found_depth);
                        }
                    }
                    stop.store(true, std::memory_order_relaxed);
                    break;
                }
                if (outcome == Search::Result::timed_out) {
                    stop.store(true, std::memory_order_relaxed);
                    break;
                }
            }
            worker_entries[index] = search.table_size();
        });
    }
    for (auto& worker : workers) worker.join();
    for (unsigned index = 0; index < options.threads; ++index) {
        const Solution& stats = worker_stats[index];
        solution.states_expanded += stats.states_expanded;
        solution.moves_generated += stats.moves_generated;
        solution.transposition_lookups += stats.transposition_lookups;
        solution.transposition_hits += stats.transposition_hits;
        solution.transposition_inserts += stats.transposition_inserts;
        solution.transposition_entries += worker_entries[index];
        solution.lower_bound_prunes += stats.lower_bound_prunes;
        solution.max_search_depth = std::max(solution.max_search_depth, stats.max_search_depth);
    }
    solution.thread_count = static_cast<int>(options.threads);
    if (!winning_path.empty()) {
        solution.moves = std::move(winning_path);
        return Search::Result::found;
    }
    if (stop.load(std::memory_order_relaxed)) return Search::Result::timed_out;
    return Search::Result::failed;
}

Solution solve_impl(const Game& game, State initial, bool prove_optimal, int target,
                    std::chrono::milliseconds time_limit, SearchOptions options) {
    if (options.threads < 1 || options.threads > 16)
        throw std::invalid_argument("thread count must be between 1 and 16");
    const bool target_mode = target >= 0;
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + time_limit;
    const bool limited = time_limit.count() > 0;
    Solution result;
    for (int variant = 0; variant < 4; ++variant) {
        auto candidate = greedy(game, initial, variant);
        if (variant == 0 || candidate.size() < result.moves.size()) {
            result.moves = std::move(candidate);
        }
        if (target_mode && static_cast<int>(result.moves.size()) <= target) break;
    }
    const int initial_lower_bound = options.column_run_bound ?
        game.column_run_lower_bound(initial) : color_lower_bound(game.generate_moves(initial));
    result.optimal = static_cast<int>(result.moves.size()) == initial_lower_bound;
    if (target_mode && target < initial_lower_bound) result.target_impossible = true;
    if (!result.optimal && !result.target_impossible &&
        (!target_mode || static_cast<int>(result.moves.size()) > target)) {
        auto candidate = beam(game, initial, static_cast<int>(result.moves.size()),
                              deadline, limited);
        if (!candidate.empty() && candidate.size() < result.moves.size()) {
            result.moves = std::move(candidate);
        }
        result.optimal = static_cast<int>(result.moves.size()) == initial_lower_bound;
    }
    result.fast_length = static_cast<int>(result.moves.size());
    if (target_mode && !result.target_impossible &&
        static_cast<int>(result.moves.size()) > target) {
        // Search the requested bound directly, rather than proving every
        // intermediate bound below the heuristic incumbent.
        const auto exact_start = std::chrono::steady_clock::now();
        Search::Result outcome;
        if (options.threads > 1 && target > 2) {
            outcome = search_parallel(game, initial, target, deadline, limited, options, result);
        } else {
            Search search{game, result, deadline, limited, options};
            search.reserve_table(65536);
            outcome = search.dfs(initial, target, 0);
            if (outcome == Search::Result::found) {
                result.moves.assign(search.path.begin(), search.path.begin() + search.found_depth);
            }
            result.transposition_entries = search.table_size();
        }
        if (outcome == Search::Result::found) {
            result.optimal = static_cast<int>(result.moves.size()) == initial_lower_bound;
        } else if (outcome == Search::Result::failed) {
            result.target_impossible = true;
            result.optimal = static_cast<int>(result.moves.size()) == target + 1;
        }
        result.exact_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - exact_start).count();
    } else if (!target_mode && prove_optimal && !result.optimal) {
        const auto exact_start = std::chrono::steady_clock::now();
        Search search{game, result, deadline, limited, options};
        search.reserve_table(65536);
        for (;;) {
            const int bound = static_cast<int>(result.moves.size()) - 1;
            if (bound < initial_lower_bound) {
                result.optimal = true;
                break;
            }
            const Search::Result outcome = search.dfs(initial, bound, 0);
            if (outcome == Search::Result::timed_out) break;
            if (outcome == Search::Result::failed) {
                result.optimal = true;
                break;
            }
            result.moves.assign(search.path.begin(), search.path.begin() + search.found_depth);
        }
        result.transposition_entries = search.table_size();
        result.exact_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - exact_start).count();
    }
    result.elapsed_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}

} // namespace

Solution Solver::solve(bool prove_optimal, std::chrono::milliseconds time_limit) const {
    return solve_from(game_.initial_state(), prove_optimal, time_limit);
}

Solution Solver::solve_from(State initial, bool prove_optimal,
                            std::chrono::milliseconds time_limit) const {
    return solve_impl(game_, initial, prove_optimal, -1, time_limit, options_);
}

Solution Solver::solve_until(int target_moves, std::chrono::milliseconds time_limit) const {
    return solve_from_until(game_.initial_state(), target_moves, time_limit);
}

Solution Solver::solve_from_until(State initial, int target_moves,
                                  std::chrono::milliseconds time_limit) const {
    if (target_moves < 0 || target_moves > kRows * kColumns) {
        throw std::invalid_argument("target moves must be between 0 and 63");
    }
    return solve_impl(game_, initial, false, target_moves, time_limit, options_);
}

} // namespace tiles
