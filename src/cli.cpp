#include "game.h"
#include "solver.h"

#include <bit>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

void usage() {
    std::cerr << "Usage: solver BOARD.txt [--optimal | --fast | --target-moves N] "
                 "[--show-steps] [--time-limit-ms N]\n"
                 "Coordinates are 1-based; row 1 is the top of the currently settled board.\n";
}

void print_board(const tiles::RenderedBoard& board) {
    for (const auto& row : board) {
        for (char tile : row) std::cout << tile;
        std::cout << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    try {
        std::string file;
        bool fast = false;
        bool optimal = false;
        bool show_steps = false;
        std::chrono::milliseconds limit{0};
        std::optional<int> target;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--fast") fast = true;
            else if (arg == "--optimal") optimal = true;
            else if (arg == "--show-steps") show_steps = true;
            else if (arg == "--target-moves") {
                if (++i == argc) throw std::invalid_argument("--target-moves needs an integer from 0 to 63");
                const std::string value = argv[i];
                std::size_t used = 0;
                const long long parsed = std::stoll(value, &used);
                if (used != value.size() || parsed < 0 || parsed > tiles::kRows * tiles::kColumns) {
                    throw std::invalid_argument("--target-moves needs an integer from 0 to 63");
                }
                target = static_cast<int>(parsed);
            } else if (arg == "--time-limit-ms") {
                if (++i == argc) throw std::invalid_argument("--time-limit-ms needs a nonnegative integer");
                const std::string value = argv[i];
                std::size_t used = 0;
                const long long milliseconds = std::stoll(value, &used);
                if (used != value.size() || milliseconds < 0) {
                    throw std::invalid_argument("--time-limit-ms needs a nonnegative integer");
                }
                limit = std::chrono::milliseconds{milliseconds};
            } else if (arg.starts_with("--")) {
                throw std::invalid_argument("unknown option: " + arg);
            } else if (file.empty()) file = arg;
            else throw std::invalid_argument("only one board file can be specified");
        }
        if (file.empty() || (fast && optimal) || (target && (fast || optimal)) ||
            (fast && limit.count() > 0)) {
            throw std::invalid_argument("supply one board file; --fast, --optimal, and "
                                        "--target-moves are exclusive; --fast cannot use a time limit");
        }
        const tiles::Game game = tiles::Game::from_file(file);
        const tiles::Solution result = target
            ? tiles::Solver(game).solve_until(*target, limit)
            : tiles::Solver(game).solve(!fast, limit);
        std::cout << "Board: 7x9\nInitial tiles: "
                  << std::popcount(game.initial_state()) << "\n\n";
        std::cout << "Fast solution found: " << result.fast_length << " moves\n";
        if (target) {
            if (result.target_impossible) {
                std::cout << "No solution in " << *target << " moves or fewer (proven)\n";
                if (result.optimal) {
                    std::cout << "Optimal solution: " << result.moves.size() << " moves\n";
                } else {
                    std::cout << "Best solution found: " << result.moves.size()
                              << " moves\nOptimality of this solution not proven\n";
                }
            } else if (result.moves.size() <= static_cast<std::size_t>(*target)) {
                std::cout << "Target reached: " << result.moves.size()
                          << " moves (limit " << *target << ")\n";
                if (result.optimal) std::cout << "Optimal solution proven\n";
                else std::cout << "Optimality not proven\n";
            } else {
                std::cout << "Best solution found: " << result.moves.size()
                          << " moves\nTime limit reached; target not reached, impossibility not proven\n";
            }
        } else if (fast) {
            std::cout << "Fast solution: " << result.moves.size() << " moves\n";
        } else if (result.optimal) {
            std::cout << "Optimal solution: " << result.moves.size() << " moves\n";
        } else {
            std::cout << "Best solution found: " << result.moves.size()
                      << " moves\nOptimality not proven\n";
        }
        tiles::State state = game.initial_state();
        for (std::size_t i = 0; i < result.moves.size(); ++i) {
            const tiles::Move& move = result.moves[i];
            bool legal = false;
            for (const tiles::Move& candidate : game.generate_moves(state)) {
                if (candidate.mask == move.mask) { legal = true; break; }
            }
            if (!legal) throw std::logic_error("solver returned an illegal move");
            std::cout << i + 1 << ". click column " << move.column + 1
                      << ", row " << move.row + 1 << ", color " << move.color
                      << ", group size " << move.size << '\n';
            state = game.apply_move(state, move);
            if (show_steps) {
                std::cout << "After move " << i + 1 << ":\n";
                print_board(game.render(state));
            }
        }
        if (!game.is_empty(state)) throw std::logic_error("solver failed to clear the board");
        std::cout << "\nStates explored: " << result.states_expanded
                  << "\nTransposition hits: " << result.transposition_hits
                  << "\nMaximum search depth: " << result.max_search_depth
                  << "\nElapsed: " << std::fixed << std::setprecision(3)
                  << result.elapsed_seconds << " s\n";
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        usage();
        return 2;
    }
}
