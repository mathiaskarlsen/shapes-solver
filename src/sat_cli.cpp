#include "sat.h"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        std::string file;
        std::optional<int> target;
        std::optional<tiles::State> initial;
        tiles::SatOptions options;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--target" || arg == "--write-cnf" || arg == "--solver" ||
                arg == "--state-hex") {
                if (++i == argc) throw std::invalid_argument("missing value for " + arg);
                const std::string value = argv[i];
                if (arg == "--target" || arg == "--state-hex") {
                    std::size_t used = 0;
                    if (value.empty() || value.front() == '-')
                        throw std::invalid_argument("invalid " + arg);
                    const auto number = std::stoull(value, &used, arg == "--target" ? 10 : 16);
                    if (used != value.size()) throw std::invalid_argument("invalid " + arg);
                    if (arg == "--target") {
                        if (target || number > 63) throw std::invalid_argument("target must be 0..63");
                        target = static_cast<int>(number);
                    } else {
                        if (initial || (number & ~tiles::kFullBoard))
                            throw std::invalid_argument("invalid original-tile survivor mask");
                        initial = number;
                    }
                } else if (arg == "--write-cnf") options.cnf_path = value;
                else options.solver_path = value;
            } else if (arg.starts_with("--")) {
                throw std::invalid_argument("unknown option: " + arg);
            } else if (file.empty()) file = arg;
            else throw std::invalid_argument("only one board file is allowed");
        }
        if (file.empty() || !target || options.cnf_path.empty()) {
            throw std::invalid_argument("usage: solver-sat BOARD.txt --target N --write-cnf FILE [--solver MINISAT] [--state-hex MASK]");
        }
        const auto game = tiles::Game::from_file(file);
        tiles::State state = initial.value_or(game.initial_state());
        const auto result = tiles::solve_sat_bounded(game, state, *target, options);
        switch (result.status) {
        case tiles::SatStatus::found: std::cout << "FOUND: " << result.moves.size() << " moves\n"; break;
        case tiles::SatStatus::proven_impossible:
            std::cout << "PROVEN IMPOSSIBLE within " << *target << " moves\n"; break;
        case tiles::SatStatus::resource_limit: std::cout << "UNKNOWN: encoding resource limit\n"; break;
        case tiles::SatStatus::unknown: std::cout << "UNKNOWN: no complete SAT proof\n"; break;
        }
        for (const auto& move : result.moves) {
            bool legal = false;
            for (const auto& candidate : game.generate_moves(state)) {
                if (candidate.mask == move.mask && candidate.row == move.row &&
                    candidate.column == move.column && candidate.color == move.color) {
                    legal = true;
                    break;
                }
            }
            if (!legal) throw std::logic_error("SAT returned an illegal click");
            std::cout << "click column " << move.column + 1 << ", row " << move.row + 1
                      << ", color " << move.color << '\n';
            state = game.apply_move(state, move);
        }
        if (result.status == tiles::SatStatus::found && !game.is_empty(state))
            throw std::logic_error("SAT path did not clear board");
        std::cout << "Variables: " << result.variables << "\nClauses: " << result.clauses
                  << "\nDetail: " << result.detail << '\n';
        return result.status == tiles::SatStatus::found ||
               result.status == tiles::SatStatus::proven_impossible ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
