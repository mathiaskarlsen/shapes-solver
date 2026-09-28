#include "bfs.h"

#include <chrono>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

std::uint64_t number(const std::string& text, const std::string& option) {
    if (text.empty() || text.front() == '-') throw std::invalid_argument("invalid " + option);
    std::size_t used = 0;
    const auto parsed = std::stoull(text, &used);
    if (used != text.size()) throw std::invalid_argument("invalid " + option);
    return parsed;
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string file;
        std::optional<int> target;
        tiles::BfsOptions options;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--target" || arg == "--memory-mb" || arg == "--time-limit-ms") {
                if (++i == argc) throw std::invalid_argument("missing value for " + arg);
                const auto value = number(argv[i], arg);
                if (arg == "--target") {
                    if (target || value > 63) throw std::invalid_argument("target must be 0..63");
                    target = static_cast<int>(value);
                } else if (arg == "--memory-mb") {
                    if (!value || value > std::numeric_limits<std::size_t>::max() / 1048576)
                        throw std::invalid_argument("memory limit out of range");
                    options.memory_limit_bytes = static_cast<std::size_t>(value) * 1048576;
                } else {
                    if (value > static_cast<std::uint64_t>(std::chrono::milliseconds::max().count()))
                        throw std::invalid_argument("time limit out of range");
                    options.time_limit = std::chrono::milliseconds{static_cast<std::int64_t>(value)};
                }
            } else if (arg.starts_with("--")) {
                throw std::invalid_argument("unknown option: " + arg);
            } else if (file.empty()) file = arg;
            else throw std::invalid_argument("only one board file is allowed");
        }
        if (file.empty() || !target) {
            throw std::invalid_argument("usage: solver-bfs BOARD.txt --target N [--memory-mb M] [--time-limit-ms N]");
        }
        const auto game = tiles::Game::from_file(file);
        const auto result = tiles::bounded_bfs(game, game.initial_state(), *target, options);
        switch (result.status) {
        case tiles::BfsStatus::FOUND: std::cout << "FOUND: " << result.moves.size() << " moves\n"; break;
        case tiles::BfsStatus::UNSAT: std::cout << "PROVEN IMPOSSIBLE within " << *target << " moves\n"; break;
        case tiles::BfsStatus::RESOURCE_LIMIT:
            std::cout << "UNKNOWN: " << (result.limit == tiles::BfsLimit::memory ?
                "memory" : "time") << " limit reached\n";
            break;
        }
        tiles::State state = game.initial_state();
        for (const auto& move : result.moves) {
            bool legal = false;
            for (const auto& candidate : game.generate_moves(state)) {
                if (candidate.mask == move.mask && candidate.color == move.color &&
                    candidate.row == move.row && candidate.column == move.column) {
                    legal = true;
                    break;
                }
            }
            if (!legal) throw std::logic_error("BFS returned an illegal click");
            std::cout << "click column " << move.column + 1 << ", row " << move.row + 1
                      << ", color " << move.color << '\n';
            state = game.apply_move(state, move);
        }
        if (result.status == tiles::BfsStatus::FOUND && !game.is_empty(state))
            throw std::logic_error("BFS solution did not clear board");
        std::cout << "depth new duplicate cumulative\n";
        for (const auto& row : result.depths) {
            std::cout << row.depth << ' ' << row.new_states << ' ' << row.duplicate_states
                      << ' ' << row.cumulative_states << '\n';
        }
        std::cout << "Expanded: " << result.states_expanded << "\nPeak budgeted memory: "
                  << result.memory_bytes / 1048576 << " MiB\nElapsed: "
                  << result.elapsed_seconds << " s\n";
        return result.status == tiles::BfsStatus::RESOURCE_LIMIT ? 2 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
