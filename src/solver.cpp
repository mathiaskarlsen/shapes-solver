#include "solver.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
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

int largest_group(const MoveList& moves) {
    int largest = 0;
    for (const Move& move : moves) largest = std::max(largest, move.size);
    return largest;
}

std::vector<Move> greedy(const Game& game, State initial, int variant) {
    std::vector<Move> path;
    path.reserve(kColumns * kRows);
    State state = initial;
    while (state) {
        const MoveList moves = game.generate_moves(state);
        int best_score = -100000;
        Move best;
        for (const Move& move : moves) {
            const MoveList next = game.generate_moves(game.apply_move(state, move));
            const int merges = moves.count - next.count - 1;
            int score = 0;
            switch (variant) {
            case 0: score = move.size * 100 + merges * 15; break;
            case 1: score = move.size * 15 + merges * 100; break;
            case 2: score = move.size * 30 + merges * 40 + largest_group(next) * 5; break;
            default: score = move.size * 70 + merges * 35 + largest_group(next) * 3; break;
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
    for (int depth = 1; depth < upper_bound && !frontier.empty(); ++depth) {
        if (limited && std::chrono::steady_clock::now() >= deadline) break;
        std::vector<BeamCandidate> candidates;
        std::unordered_set<State> seen;
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
                const MoveList groups = game.generate_moves(child);
                if (depth + color_lower_bound(groups) >= upper_bound) continue;
                const int tiles = std::popcount(child);
                const int largest = largest_group(groups);
                const int score = 110 * groups.count + 2 * tiles - 5 * largest;
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
            const MoveList next = game.generate_moves(game.apply_move(state, move));
            ordered[i] = {move, move.size * 16 +
                        (moves.count - next.count - 1) * 24 + largest_group(next) * 2};
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

} // namespace

Solution Solver::solve(bool prove_optimal, std::chrono::milliseconds time_limit) const {
    return solve_from(game_.initial_state(), prove_optimal, time_limit);
}

Solution Solver::solve_from(State initial, bool prove_optimal,
                            std::chrono::milliseconds time_limit) const {
    const auto start = std::chrono::steady_clock::now();
    Solution result;
    for (int variant = 0; variant < 4; ++variant) {
        auto candidate = greedy(game_, initial, variant);
        if (variant == 0 || candidate.size() < result.moves.size()) {
            result.moves = std::move(candidate);
        }
    }
    const int initial_lower_bound = color_lower_bound(game_.generate_moves(initial));
    result.optimal = static_cast<int>(result.moves.size()) == initial_lower_bound;
    const auto deadline = start + time_limit;
    if (!result.optimal) {
        auto candidate = beam(game_, initial, static_cast<int>(result.moves.size()),
                              deadline, time_limit.count() > 0);
        if (!candidate.empty() && candidate.size() < result.moves.size()) {
            result.moves = std::move(candidate);
        }
        result.optimal = static_cast<int>(result.moves.size()) == initial_lower_bound;
    }
    result.fast_length = static_cast<int>(result.moves.size());
    if (prove_optimal && !result.optimal) {
        Search search{game_, result, deadline, time_limit.count() > 0};
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

} // namespace tiles
