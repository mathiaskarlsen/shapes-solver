#include "bfs.h"
#include "game.h"
#include "sat.h"
#include "solver.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <unordered_map>
#include <vector>

namespace {

using tiles::Game;
using tiles::Move;
using tiles::MoveList;
using tiles::RenderedBoard;
using tiles::Solver;
using tiles::State;

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

State bit(int row, int column) {
    return State{1} << (row * tiles::kColumns + column);
}

State mask(std::initializer_list<std::pair<int, int>> positions) {
    State result = 0;
    for (const auto& [row, column] : positions) {
        result |= bit(row, column);
    }
    return result;
}

using Rows = std::array<std::string, tiles::kRows>;

Rows board(std::initializer_list<std::tuple<int, int, char>> overrides = {}) {
    Rows rows;
    rows.fill(std::string(tiles::kColumns, 'P'));
    for (const auto& [row, column, color] : overrides) {
        rows[row][column] = color;
    }
    return rows;
}

const Move& find_move(const MoveList& moves, State component) {
    for (const Move& move : moves) {
        if (move.mask == component) {
            return move;
        }
    }
    throw std::runtime_error("expected removable component was not generated");
}

void replay(const Game& game, State state, const tiles::Solution& solution) {
    for (const Move& move : solution.moves) {
        const MoveList legal = game.generate_moves(state);
        const Move& generated = find_move(legal, move.mask);
        check(generated.color == move.color && generated.row == move.row
                  && generated.column == move.column && generated.size == move.size,
              "solution move must describe a legal current component");
        const State next = game.apply_move(state, move);
        check(next != state && (next & ~state) == 0, "solution move did not remove live tiles");
        state = next;
    }
    check(game.is_empty(state), "replaying solution did not clear the board");
}

void test_singleton_after_mask() {
    const Game game(board({{1, 4, 'B'}}));
    const State state = mask({{1, 4}});
    const auto rendered = game.render(state);
    check(rendered[8][4] == 'B' && rendered[1][4] == '.',
          "single surviving tile must fall to the bottom of its column");
    const MoveList moves = game.generate_moves(state);
    check(moves.count == 1 && moves.moves[0].mask == state && moves.moves[0].size == 1,
          "singleton must have exactly one size-one move");
    check(moves.moves[0].row == 8 && moves.moves[0].column == 4,
          "singleton move must identify its settled coordinate");
    check(game.is_empty(game.apply_move(state, moves.moves[0])),
          "removing singleton must clear state");
}

void test_adjacent_pair() {
    const Game game(board({{8, 2, 'B'}, {8, 3, 'B'}}));
    const State state = mask({{8, 2}, {8, 3}});
    const MoveList moves = game.generate_moves(state);
    check(moves.count == 1 && moves.moves[0].mask == state && moves.moves[0].size == 2,
          "adjacent equal-colored tiles must form one pair component");
    check(game.apply_move(state, moves.moves[0]) == 0,
          "pair removal must remove both original tile IDs");
}

void test_separation_and_gravity_connection() {
    {
        const Game game(board({{8, 0, 'G'}, {8, 2, 'G'}}));
        const State state = mask({{8, 0}, {8, 2}});
        const MoveList moves = game.generate_moves(state);
        check(moves.count == 2, "gravity must not connect same-colored tiles across an empty column");
        find_move(moves, bit(8, 0));
        find_move(moves, bit(8, 2));
        const auto solution = Solver(game).solve_from(state);
        check(solution.optimal && solution.moves.size() == 2,
              "two separated equal-colored tiles need two removals");
        replay(game, state, solution);
    }
    {
        const Game game(board({{0, 3, 'G'}, {4, 3, 'B'}, {8, 3, 'G'}}));
        const State state = mask({{0, 3}, {4, 3}, {8, 3}});
        check(game.generate_moves(state).count == 3,
              "blocker must separate the two green tiles initially");
        const State next = game.apply_move(state, find_move(game.generate_moves(state), bit(4, 3)));
        const MoveList connected = game.generate_moves(next);
        check(connected.count == 1 && connected.moves[0].mask == mask({{0, 3}, {8, 3}})
                  && connected.moves[0].size == 2,
              "removing blocker must settle the green tiles into one component");
        check(game.render(next)[7][3] == 'G' && game.render(next)[8][3] == 'G',
              "connected green pair must occupy adjacent bottom cells");
        const auto solution = Solver(game).solve_from(state);
        check(solution.optimal && solution.moves.size() == 2,
              "removing blocker before green pair must be optimal");
        replay(game, state, solution);
    }
}

void test_gravity_order_and_no_horizontal_shift() {
    {
        const Game game(board({{0, 1, 'G'}, {2, 1, 'B'}, {5, 1, 'O'}, {8, 1, 'P'}}));
        const State state = mask({{0, 1}, {2, 1}, {5, 1}, {8, 1}});
        const RenderedBoard before = game.render(state);
        for (int row = 0; row < 5; ++row) {
            check(before[row][1] == '.', "gravity must leave empty cells above the stack");
        }
        check(before[5][1] == 'G' && before[6][1] == 'B'
                  && before[7][1] == 'O' && before[8][1] == 'P',
              "gravity must preserve original vertical order");
        const State next = game.apply_move(state, find_move(game.generate_moves(state), bit(2, 1)));
        const RenderedBoard after = game.render(next);
        check(after[5][1] == '.' && after[6][1] == 'G'
                  && after[7][1] == 'O' && after[8][1] == 'P',
              "gravity must compact remaining tiles without changing their order");
    }
    {
        const Game game(board({{8, 0, 'B'}, {8, 1, 'G'}}));
        const State state = mask({{8, 0}, {8, 1}});
        const State next = game.apply_move(state, find_move(game.generate_moves(state), bit(8, 0)));
        const RenderedBoard rendered = game.render(next);
        check(rendered[8][0] == '.' && rendered[8][1] == 'G',
              "an empty column must remain empty: no horizontal shift");
    }
}

void test_any_tile_of_component_has_equal_successor() {
    const Game game(board({{8, 1, 'B'}, {8, 2, 'B'}, {8, 3, 'B'}, {7, 2, 'O'}}));
    const State state = mask({{8, 1}, {8, 2}, {8, 3}, {7, 2}});
    const State component = mask({{8, 1}, {8, 2}, {8, 3}});
    const Move move = find_move(game.generate_moves(state), component);
    const State expected = bit(7, 2);
    for (int column = 1; column <= 3; ++column) {
        check(game.render(state)[8][column] == 'B',
              "each chosen coordinate must contain the same component color");
        Move chosen_tile = move;
        chosen_tile.row = 8;
        chosen_tile.column = column;
        check(game.apply_move(state, chosen_tile) == expected,
              "choosing any tile in a component must yield the same successor");
        const RenderedBoard after = game.render(game.apply_move(state, chosen_tile));
        check(after[8][1] == '.' && after[8][2] == 'O' && after[8][3] == '.',
              "component removal must clear all its cells and settle the tile above");
    }
}

void test_one_move_per_component() {
    const Game game(board({{8, 0, 'B'}, {8, 1, 'B'}, {8, 3, 'G'},
                           {8, 4, 'G'}, {8, 6, 'O'}}));
    const State state = mask({{8, 0}, {8, 1}, {8, 3}, {8, 4}, {8, 6}});
    const MoveList moves = game.generate_moves(state);
    check(moves.count == 3, "generator must emit one move, not one per tile, for each component");
    find_move(moves, mask({{8, 0}, {8, 1}}));
    find_move(moves, mask({{8, 3}, {8, 4}}));
    find_move(moves, bit(8, 6));
}

void test_empty_state() {
    const Game game(board());
    check(game.is_empty(0), "zero mask must be empty");
    check(game.generate_moves(0).count == 0, "empty state must generate no moves");
    for (const auto& row : game.render(0)) {
        for (char cell : row) {
            check(cell == '.', "empty state must render only dots");
        }
    }
    const auto solution = Solver(game).solve_from(0);
    check(solution.optimal && solution.moves.empty(), "empty state has exact optimum zero");
    replay(game, 0, solution);
}

void test_small_known_optima_and_replay() {
    const Game game(board({{0, 1, 'G'}, {4, 1, 'B'}, {8, 1, 'G'},
                           {8, 3, 'O'}, {8, 5, 'B'}}));
    const std::array<std::pair<State, std::size_t>, 3> cases{{
        {bit(0, 1), 1},
        {mask({{0, 1}, {4, 1}, {8, 1}}), 2},
        {mask({{8, 1}, {8, 3}, {8, 5}}), 3},
    }};
    for (const auto& [state, optimum] : cases) {
        const auto solution = Solver(game).solve_from(state);
        check(solution.optimal && solution.moves.size() == optimum,
              "exact solver returned an incorrect small known optimum");
        replay(game, state, solution);
    }
}

void test_target_reached_and_ruled_out() {
    const Game game(board({{8, 0, 'G'}, {8, 2, 'G'}}));
    const State state = mask({{8, 0}, {8, 2}});

    const auto reached = Solver(game).solve_from_until(state, 2);
    check(!reached.target_impossible && reached.moves.size() == 2,
          "target search must stop at a reachable length");
    replay(game, state, reached);

    const auto ruled_out = Solver(game).solve_from_until(state, 1);
    check(ruled_out.target_impossible && ruled_out.optimal && ruled_out.moves.size() == 2,
          "exhausting a bound one below the incumbent proves its optimality");
    replay(game, state, ruled_out);

    const auto weaker_bound = Solver(game).solve_from_until(state, 0);
    check(weaker_bound.target_impossible,
          "nonempty board cannot clear in zero clicks");
    const auto empty = Solver(game).solve_from_until(0, 0);
    check(!empty.target_impossible && empty.optimal && empty.moves.empty(),
          "empty state must meet a zero-click target");
}

void test_generated_example_fast_solution() {
    const Game game(Rows{
        "BPOOGOO", "GOOBGBB", "BOPPPGP",
        "OPGGPPG", "BOBPGGG", "BBGGBBP",
        "BGPGBPO", "BGBPOPG", "PPBPPGB"
    });
    const auto solution = Solver(game).solve(false);
    check(solution.moves.size() <= 12,
          "generated example must have a complete solution in at most 12 clicks");
    replay(game, game.initial_state(), solution);
    const auto targeted = Solver(game).solve_until(12);
    check(!targeted.target_impossible && targeted.moves.size() <= 12,
          "target mode must find a complete solution within the requested length");
    replay(game, game.initial_state(), targeted);
}

std::uint64_t next_random(std::uint64_t& seed) {
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}

void test_canonical_visible_states() {
    const Game game(board({{1, 0, 'G'}, {8, 1, 'B'}, {4, 3, 'O'}, {6, 5, 'G'}}));
    const State upper = mask({{0, 0}, {8, 1}});
    const State lower = mask({{2, 0}, {8, 1}});
    check(game.canonical_key(upper) == game.canonical_key(lower) &&
              game.render(upper) == game.render(lower),
          "surviving interchangeable P tiles must share a visible-board key");
    check(game.canonical_key(upper) != game.canonical_key(mask({{1, 0}, {8, 1}})),
          "different visible colors must have different keys");

    const auto successors = [&](State state) {
        std::vector<State> keys;
        for (const Move& move : game.generate_moves(state)) {
            keys.push_back(game.canonical_key(game.apply_move(state, move)));
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    };
    check(successors(upper) == successors(lower),
          "equivalent boards must have equivalent legal successor boards");

    for (int column = 0; column < tiles::kColumns; ++column) {
        std::unordered_map<std::string, State> key_by_sequence;
        std::unordered_map<State, std::string> sequence_by_key;
        for (int local = 0; local < (1 << tiles::kRows); ++local) {
            State state = 0;
            for (int row = 0; row < tiles::kRows; ++row) {
                if (local & (1 << row)) state |= bit(row, column);
            }
            const auto rendered = game.render(state);
            std::string sequence;
            for (int row = 0; row < tiles::kRows; ++row) {
                if (rendered[row][column] != '.') sequence += rendered[row][column];
            }
            const State key = game.canonical_key(state);
            const auto [by_sequence, new_sequence] = key_by_sequence.emplace(sequence, key);
            const auto [by_key, new_key] = sequence_by_key.emplace(key, sequence);
            check((new_sequence || by_sequence->second == key) &&
                      (new_key || by_key->second == sequence),
                  "column keys must match compacted color sequences exactly");
        }
    }
    std::uint64_t seed = 0x19879c562eb1a573ULL;
    for (int trial = 0; trial < 200; ++trial) {
        const State state = next_random(seed) & tiles::kFullBoard;
        const State key = game.canonical_key(state);
        State representative = 0;
        for (int column = 0; column < tiles::kColumns; ++column) {
            const State local = (key >> (column * tiles::kRows)) & 511;
            for (int row = 0; row < tiles::kRows; ++row) {
                if (local & (State{1} << row)) representative |= bit(row, column);
            }
        }
        check(game.canonical_key(representative) == key &&
                  game.render(representative) == game.render(state) &&
                  successors(representative) == successors(state),
              "random equivalent states must share visible boards and successor keys");
    }
}

void test_canonical_bounded_proofs() {
    const Game game(Rows{
        "BPOOGOO", "GOOBGBB", "BOPPPGP",
        "OPGGPPG", "BOBPGGG", "BBGGBBP",
        "BGPGBPO", "BGBPOPG", "PPBPPGB"
    });
    std::uint64_t seed = 0x873105dc901a147bULL;
    for (int trial = 0; trial < 24; ++trial) {
        State state = 0;
        for (int i = 0; i < 9; ++i) state |= State{1} << (next_random(seed) % 63);
        for (int target = 0; target <= 5; ++target) {
            const auto raw = Solver(game).solve_from_until(state, target);
            const auto canonical = Solver(game, tiles::SearchOptions{true})
                                       .solve_from_until(state, target);
            check(raw.target_impossible == canonical.target_impossible &&
                      (raw.moves.size() <= static_cast<std::size_t>(target)) ==
                          (canonical.moves.size() <= static_cast<std::size_t>(target)),
                  "raw and canonical DFS must agree on bounded SAT/UNSAT");
            replay(game, state, canonical);
            const auto flat = Solver(game, tiles::SearchOptions{true, false, true})
                                  .solve_from_until(state, target);
            check(raw.target_impossible == flat.target_impossible &&
                      (raw.moves.size() <= static_cast<std::size_t>(target)) ==
                          (flat.moves.size() <= static_cast<std::size_t>(target)),
                  "flat failure table must preserve bounded proof results");
            replay(game, state, flat);
            const auto parallel = Solver(game, tiles::SearchOptions{true, true, true, 4})
                                      .solve_from_until(state, target);
            check(raw.target_impossible == parallel.target_impossible &&
                      (raw.moves.size() <= static_cast<std::size_t>(target)) ==
                          (parallel.moves.size() <= static_cast<std::size_t>(target)),
                  "parallel frontier must preserve bounded proof results");
            replay(game, state, parallel);
        }
    }
}

void test_bounded_bfs() {
    const Game game(Rows{
        "BPOOGOO", "GOOBGBB", "BOPPPGP",
        "OPGGPPG", "BOBPGGG", "BBGGBBP",
        "BGPGBPO", "BGBPOPG", "PPBPPGB"
    });
    check(tiles::bounded_bfs(game, 0, 0).status == tiles::BfsStatus::FOUND,
          "empty board needs zero moves");
    const auto insufficient = tiles::bounded_bfs(
        game, bit(0, 0), 1, tiles::BfsOptions{1, std::chrono::milliseconds{0}});
    check(insufficient.status == tiles::BfsStatus::RESOURCE_LIMIT,
          "BFS must report a tight memory limit, not an impossible target");
    std::uint64_t seed = 0x873105dc901a147bULL;
    for (int trial = 0; trial < 24; ++trial) {
        State state = 0;
        for (int i = 0; i < 7; ++i) state |= State{1} << (next_random(seed) % 63);
        for (int target = 0; target <= 5; ++target) {
            const auto dfs = Solver(game).solve_from_until(state, target);
            const auto bfs = tiles::bounded_bfs(game, state, target);
            check(bfs.status != tiles::BfsStatus::RESOURCE_LIMIT &&
                      (bfs.status == tiles::BfsStatus::FOUND) ==
                          (dfs.moves.size() <= static_cast<std::size_t>(target)) &&
                      (bfs.status == tiles::BfsStatus::UNSAT) == dfs.target_impossible,
                  "bounded BFS and DFS must agree on reachable short boards");
            if (bfs.status == tiles::BfsStatus::FOUND) {
                tiles::Solution replayable;
                replayable.moves = bfs.moves;
                replay(game, state, replayable);
            }
        }
    }
}

void test_sat_cross_validation() {
    const char* backend = std::getenv("SHAPES_SAT_SOLVER");
    if (!backend || !*backend) return; // External CDCL solver is optional.
    const Game game(Rows{
        "BPOOGOO", "GOOBGBB", "BOPPPGP",
        "OPGGPPG", "BOBPGGG", "BBGGBBP",
        "BGPGBPO", "BGBPOPG", "PPBPPGB"
    });
    const auto path = (std::filesystem::temp_directory_path() /
        ("shapes-sat-check-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".cnf")).string();
    const auto compare = [&](const Game& example, State state, int target) {
        const auto dfs = Solver(example).solve_from_until(state, target);
        const auto sat = tiles::solve_sat_bounded(
            example, state, target, tiles::SatOptions{path, backend});
        std::filesystem::remove(path);
        check((sat.status == tiles::SatStatus::found) ==
                  (dfs.moves.size() <= static_cast<std::size_t>(target)) &&
                  (sat.status == tiles::SatStatus::proven_impossible) ==
                      dfs.target_impossible,
              "bounded SAT and DFS must agree: " + sat.detail);
        if (sat.status == tiles::SatStatus::found) {
            tiles::Solution replayable;
            replayable.moves = sat.moves;
            replay(example, state, replayable);
        }
    };
    compare(game, 0, 0);
    const Game merging(board({{8, 1, 'B'}}));
    const State merge_state = mask({{8, 0}, {8, 1}, {7, 1}});
    compare(merging, merge_state, 1);
    compare(merging, merge_state, 2);
    compare(merging, merge_state, 3);
    std::uint64_t seed = 0x29eb319a638a24d1ULL;
    for (int trial = 0; trial < 12; ++trial) {
        State state = 0;
        for (int i = 0; i < 6; ++i) state |= State{1} << (next_random(seed) % 63);
        for (int target = 0; target <= 4; ++target) {
            compare(game, state, target);
        }
    }
}

void test_column_run_bound() {
    const Game separated(board({{0, 0, 'G'}, {0, 2, 'G'},
                                {0, 4, 'G'}, {0, 5, 'G'}}));
    const State state = mask({{0, 0}, {0, 2}, {0, 4}, {0, 5}});
    check(separated.column_run_lower_bound(0) == 0 &&
              separated.column_run_lower_bound(state) == 3,
          "separate same-color column ranges require separate clicks");
    const auto example = Solver(separated).solve_from(state);
    check(example.optimal && example.moves.size() == 3,
          "three disjoint column runs need three clicks");

    const Game game(Rows{
        "BPOOGOO", "GOOBGBB", "BOPPPGP",
        "OPGGPPG", "BOBPGGG", "BBGGBBP",
        "BGPGBPO", "BGBPOPG", "PPBPPGB"
    });
    std::uint64_t seed = 0x2ab0635a771e48d1ULL;
    for (int trial = 0; trial < 64; ++trial) {
        State sparse = 0;
        for (int i = 0; i < 8; ++i) sparse |= State{1} << (next_random(seed) % 63);
        const int bound = game.column_run_lower_bound(sparse);
        const auto optimal = Solver(game).solve_from(sparse);
        check(optimal.optimal && bound <= static_cast<int>(optimal.moves.size()),
              "column-run bound must not overestimate exact optimum");
        const int target = bound - 1;
        if (target >= 0) {
            const auto checked = Solver(game, tiles::SearchOptions{false, true})
                                     .solve_from_until(sparse, target);
            check(checked.target_impossible,
                  "column-run bound must soundly disprove smaller targets");
        }
    }
}

void test_random_components_and_gravity() {
    std::uint64_t seed = 0x923ace671bdf1054ULL;
    constexpr char colors[] = "PBGO";
    for (int trial = 0; trial < 80; ++trial) {
        Rows rows = board();
        State state = 0;
        for (int row = 0; row < tiles::kRows; ++row) {
            for (int column = 0; column < tiles::kColumns; ++column) {
                rows[row][column] = colors[next_random(seed) % 4];
                if (next_random(seed) % 3 == 0) {
                    state |= bit(row, column);
                }
            }
        }
        const Game game(rows);
        const RenderedBoard rendered = game.render(state);
        std::array<std::array<int, tiles::kColumns>, tiles::kRows> ids;
        for (auto& row : ids) {
            row.fill(-1);
        }
        for (int column = 0; column < tiles::kColumns; ++column) {
            int settled_row = tiles::kRows - 1;
            for (int row = tiles::kRows - 1; row >= 0; --row) {
                if (state & bit(row, column)) {
                    ids[settled_row][column] = row * tiles::kColumns + column;
                    check(rendered[settled_row][column] == rows[row][column],
                          "random gravity changed original tile order or color");
                    --settled_row;
                }
            }
            for (int row = 0; row <= settled_row; ++row) {
                check(rendered[row][column] == '.', "random gravity left a gap in a column");
            }
        }

        std::vector<State> expected;
        std::array<std::array<bool, tiles::kColumns>, tiles::kRows> visited{};
        constexpr int dr[] = {-1, 1, 0, 0};
        constexpr int dc[] = {0, 0, -1, 1};
        for (int row = 0; row < tiles::kRows; ++row) {
            for (int column = 0; column < tiles::kColumns; ++column) {
                if (ids[row][column] < 0 || visited[row][column]) {
                    continue;
                }
                const char color = rendered[row][column];
                std::vector<std::pair<int, int>> queue{{row, column}};
                visited[row][column] = true;
                State component = 0;
                for (std::size_t i = 0; i < queue.size(); ++i) {
                    const auto [r, c] = queue[i];
                    component |= State{1} << ids[r][c];
                    for (int direction = 0; direction < 4; ++direction) {
                        const int nr = r + dr[direction];
                        const int nc = c + dc[direction];
                        if (nr >= 0 && nr < tiles::kRows && nc >= 0 && nc < tiles::kColumns
                            && !visited[nr][nc] && rendered[nr][nc] == color) {
                            visited[nr][nc] = true;
                            queue.emplace_back(nr, nc);
                        }
                    }
                }
                expected.push_back(component);
            }
        }
        int largest = 0;
        for (State component : expected) largest = std::max(largest, std::popcount(component));
        bool present[4]{};
        for (const auto& row : rendered) {
            for (char color : row) {
                if (color == 'P') present[0] = true;
                if (color == 'B') present[1] = true;
                if (color == 'G') present[2] = true;
                if (color == 'O') present[3] = true;
            }
        }
        const int colors = std::count(present, present + 4, true);
        const auto metrics = game.measure(state);
        check(metrics.components == static_cast<int>(expected.size()) &&
                  metrics.largest == largest && metrics.distinct_colors == colors,
              "random board metrics must describe the settled components");
        const MoveList moves = game.generate_moves(state);
        std::vector<State> actual;
        for (const Move& move : moves) {
            check(move.mask != 0 && (move.mask & ~state) == 0
                      && move.size == std::popcount(move.mask),
                  "random move mask/size must describe surviving original tiles");
            check(move.row >= 0 && move.row < tiles::kRows
                      && move.column >= 0 && move.column < tiles::kColumns
                      && rendered[move.row][move.column] == move.color
                      && ids[move.row][move.column] >= 0
                      && (move.mask & (State{1} << ids[move.row][move.column])) != 0,
                  "random move representative must be inside its component");
            actual.push_back(move.mask);
        }
        std::sort(expected.begin(), expected.end());
        std::sort(actual.begin(), actual.end());
        check(actual == expected,
              "random component masks must match monochrome 4-connected settled regions");
    }
}

} // namespace

int main() {
    try {
        test_singleton_after_mask();
        test_adjacent_pair();
        test_separation_and_gravity_connection();
        test_gravity_order_and_no_horizontal_shift();
        test_any_tile_of_component_has_equal_successor();
        test_one_move_per_component();
        test_empty_state();
        test_small_known_optima_and_replay();
        test_target_reached_and_ruled_out();
        test_generated_example_fast_solution();
        test_canonical_visible_states();
        test_canonical_bounded_proofs();
        test_column_run_bound();
        test_bounded_bfs();
        test_sat_cross_validation();
        test_random_components_and_gravity();
    } catch (const std::exception& error) {
        std::cerr << "test_solver: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
