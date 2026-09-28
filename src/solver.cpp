#include "solver.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <stdexcept>
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

struct Search {
    enum class Result { failed, found, timed_out };
    const Game& game;
    Solution& solution;
    std::chrono::steady_clock::time_point deadline;
    bool limited;
    std::unordered_map<State, unsigned char> failed;
    std::array<Move, kColumns * kRows> path{};
    int found_depth = 0;

    Result dfs(State state, int left, int depth) {
        if (!state) {
            found_depth = depth;
            return Result::found;
        }
        if (left == 0) return Result::failed;
        // Checking periodically avoids a clock call per recursive node.
        if (limited && (solution.states_expanded & 1023) == 0 &&
            std::chrono::steady_clock::now() >= deadline) return Result::timed_out;
        auto it = failed.find(state);
        if (it != failed.end() && it->second >= left) {
            ++solution.transposition_hits;
            return Result::failed;
        }
        ++solution.states_expanded;
        solution.max_search_depth = std::max(solution.max_search_depth, depth);
        const MoveList moves = game.generate_moves(state);
        if (color_lower_bound(moves) > left) {
            record_failure(state, left);
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
        record_failure(state, left);
        return Result::failed;
    }

    void record_failure(State state, int left) {
        auto [it, inserted] = failed.try_emplace(state, static_cast<unsigned char>(left));
        if (!inserted && it->second < left) it->second = static_cast<unsigned char>(left);
    }
};

Solution solve_impl(const Game& game, State initial, bool prove_optimal, int target,
                    std::chrono::milliseconds time_limit) {
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
    const int initial_lower_bound = color_lower_bound(game.generate_moves(initial));
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
        Search search{game, result, deadline, limited};
        search.failed.reserve(65536);
        const Search::Result outcome = search.dfs(initial, target, 0);
        if (outcome == Search::Result::found) {
            result.moves.assign(search.path.begin(), search.path.begin() + search.found_depth);
            result.optimal = static_cast<int>(result.moves.size()) == initial_lower_bound;
        } else if (outcome == Search::Result::failed) {
            result.target_impossible = true;
            result.optimal = static_cast<int>(result.moves.size()) == target + 1;
        }
    } else if (!target_mode && prove_optimal && !result.optimal) {
        Search search{game, result, deadline, limited};
        search.failed.reserve(65536);
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
    return solve_impl(game_, initial, prove_optimal, -1, time_limit);
}

Solution Solver::solve_until(int target_moves, std::chrono::milliseconds time_limit) const {
    return solve_from_until(game_.initial_state(), target_moves, time_limit);
}

Solution Solver::solve_from_until(State initial, int target_moves,
                                  std::chrono::milliseconds time_limit) const {
    if (target_moves < 0 || target_moves > kRows * kColumns) {
        throw std::invalid_argument("target moves must be between 0 and 63");
    }
    return solve_impl(game_, initial, false, target_moves, time_limit);
}

} // namespace tiles
