#include "game.h"
#include "solver.h"

#include <bit>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace {

void usage() {
    std::cerr << "Usage: solver BOARD.txt [--optimal | --fast | --target-moves N] "
                 "[--show-steps] [--time-limit-ms N] [--canonical-tt] [--distinct-color-lb] [--unordered-tt] [--threads N]\n"
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
        bool canonical_tt = false;
        bool column_run_bound = true;
        bool flat_table = true;
        unsigned threads = 1;
        std::chrono::milliseconds limit{0};
        std::optional<int> target;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--fast") fast = true;
            else if (arg == "--optimal") optimal = true;
            else if (arg == "--show-steps") show_steps = true;
            else if (arg == "--canonical-tt") canonical_tt = true;
            else if (arg == "--distinct-color-lb") column_run_bound = false;
            else if (arg == "--unordered-tt") flat_table = false;
            else if (arg == "--threads") {
                if (++i == argc) throw std::invalid_argument("--threads needs an integer from 1 to 16");
                const std::string value = argv[i];
                std::size_t used = 0;
                const long long parsed = std::stoll(value, &used);
                if (used != value.size() || parsed < 1 || parsed > 16) {
                    throw std::invalid_argument("--threads needs an integer from 1 to 16");
                }
                threads = static_cast<unsigned>(parsed);
            } else if (arg == "--target-moves") {
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
            (fast && (limit.count() > 0 || canonical_tt || !column_run_bound || !flat_table || threads != 1))) {
            throw std::invalid_argument("supply one board file; --fast, --optimal, and "
                                        "--target-moves are exclusive; --fast cannot use "
                                        "a time limit or exact-search options");
        }
        if (threads != 1 && !target)
            throw std::invalid_argument("--threads >1 requires --target-moves");
        const tiles::Game game = tiles::Game::from_file(file);
        tiles::SearchOptions options;
        options.canonical_keys = canonical_tt;
        options.column_run_bound = column_run_bound;
        options.flat_table = flat_table;
        options.threads = threads;
        const tiles::Solver solver(game, options);
        const tiles::Solution result = target
            ? solver.solve_until(*target, limit)
            : solver.solve(!fast, limit);
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
                  << "\nStates/second (exact): " << std::fixed << std::setprecision(0)
                  << (result.exact_seconds > 0 ? result.states_expanded / result.exact_seconds : 0)
                  << "\nMoves generated (exact): " << result.moves_generated
                  << "\nCanonical duplicate children: " << result.canonical_duplicate_children
                  << "\nTransposition lookups: " << result.transposition_lookups
                  << "\nTransposition hits: " << result.transposition_hits
                  << "\nTransposition inserts: " << result.transposition_inserts
                  << "\nTransposition entries: " << result.transposition_entries
                  << "\nLower-bound prunes: " << result.lower_bound_prunes
                  << "\nMaximum search depth: " << result.max_search_depth
                  << "\nThreads: " << result.thread_count
                  << "\nExact elapsed: " << std::fixed << std::setprecision(3)
                  << result.exact_seconds << " s"
                  << "\nElapsed: " << result.elapsed_seconds << " s\n";
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS memory{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
            std::cout << "Peak working set: " << memory.PeakWorkingSetSize / 1048576 << " MiB\n";
        }
#endif
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        usage();
        return 2;
    }
}
