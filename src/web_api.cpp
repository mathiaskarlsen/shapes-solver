#include "game.h"
#include "solver.h"

#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void write_positions(tiles::State state) {
    bool first = true;
    std::cout << '[';
    for (int column = 0; column < tiles::kColumns; ++column) {
        int settled_row = tiles::kRows - 1;
        for (int row = tiles::kRows - 1; row >= 0; --row) {
            const int id = row * tiles::kColumns + column;
            if (!(state & (tiles::State{1} << id))) continue;
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"id\":" << id << ",\"row\":" << settled_row-- << '}';
        }
    }
    std::cout << ']';
}

void write_removed(tiles::State mask) {
    bool first = true;
    std::cout << '[';
    for (int id = 0; id < tiles::kRows * tiles::kColumns; ++id) {
        if (!(mask & (tiles::State{1} << id))) continue;
        if (!first) std::cout << ',';
        first = false;
        std::cout << id;
    }
    std::cout << ']';
}

} // namespace

int main() {
    try {
        std::array<std::string, tiles::kRows> rows;
        tiles::State initial = 0;
        for (int row = 0; row < tiles::kRows; ++row) {
            if (!std::getline(std::cin, rows[row])) {
                throw std::invalid_argument("board must contain exactly 9 lines");
            }
            if (!rows[row].empty() && rows[row].back() == '\r') rows[row].pop_back();
            if (rows[row].size() != tiles::kColumns) {
                throw std::invalid_argument("row " + std::to_string(row + 1) +
                                            " must contain exactly 7 characters");
            }
            for (int column = 0; column < tiles::kColumns; ++column) {
                char& tile = rows[row][column];
                if (tile == '.') {
                    tile = 'P'; // Game requires colors for every original ID, even absent ones.
                } else if (tile == 'P' || tile == 'B' || tile == 'G' || tile == 'O') {
                    initial |= tiles::State{1} << (row * tiles::kColumns + column);
                } else {
                    throw std::invalid_argument("invalid color at row " +
                                                std::to_string(row + 1) + ", column " +
                                                std::to_string(column + 1) +
                                                ": expected P, B, G, O, or .");
                }
            }
        }
        std::string extra;
        if (std::getline(std::cin, extra)) {
            throw std::invalid_argument("board must contain exactly 9 lines (extra line found)");
        }
        const tiles::Game game(rows);
        const tiles::Solution solution = tiles::Solver(game).solve_from(initial, false);

        // Emit only after the solver finishes, so errors never result in partial JSON.
        std::cout << "{\"length\":" << solution.moves.size() << ",\"initial\":";
        write_positions(initial);
        std::cout << ",\"steps\":[";
        tiles::State state = initial;
        bool first = true;
        for (const tiles::Move& move : solution.moves) {
            if (!first) std::cout << ',';
            first = false;
            state = game.apply_move(state, move);
            std::cout << "{\"row\":" << move.row << ",\"column\":" << move.column
                      << ",\"color\":\"" << move.color << "\",\"size\":" << move.size
                      << ",\"removed\":";
            write_removed(move.mask);
            std::cout << ",\"positions\":";
            write_positions(state);
            std::cout << '}';
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 2;
    }
}
