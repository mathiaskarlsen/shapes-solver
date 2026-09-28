#include "game.h"

#include <fstream>
#include <stdexcept>

namespace tiles {

Game::Game(const std::array<std::string, kRows>& rows) {
    for (int r = 0; r < kRows; ++r) {
        if (rows[r].size() != kColumns) {
            throw std::invalid_argument("row " + std::to_string(r + 1) + " must contain exactly 7 characters");
        }
        for (int c = 0; c < kColumns; ++c) {
            const char ch = rows[r][c];
            if (ch != 'P' && ch != 'B' && ch != 'G' && ch != 'O') {
                throw std::invalid_argument("invalid color at row " + std::to_string(r + 1) +
                                            ", column " + std::to_string(c + 1) +
                                            ": expected P, B, G, or O");
            }
            colors_[r * kColumns + c] = ch;
        }
    }
}

Game Game::from_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open board file: " + path);
    std::array<std::string, kRows> rows;
    for (int r = 0; r < kRows; ++r) {
        if (!std::getline(input, rows[r])) {
            throw std::invalid_argument("board must contain exactly 9 lines (only " +
                                        std::to_string(r) + " found)");
        }
        if (!rows[r].empty() && rows[r].back() == '\r') rows[r].pop_back();
    }
    std::string extra;
    if (std::getline(input, extra)) {
        throw std::invalid_argument("board must contain exactly 9 lines (extra line found)");
    }
    return Game(rows);
}

void Game::settle(State state, RenderedBoard& board,
                  std::array<std::array<int, kColumns>, kRows>& ids) const {
    for (int r = 0; r < kRows; ++r) {
        board[r].fill('.');
        ids[r].fill(-1);
    }
    for (int c = 0; c < kColumns; ++c) {
        int bottom = kRows - 1;
        for (int r = kRows - 1; r >= 0; --r) {
            const int id = r * kColumns + c;
            if (state & (State{1} << id)) {
                board[bottom][c] = colors_[id];
                ids[bottom][c] = id;
                --bottom;
            }
        }
    }
}

RenderedBoard Game::render(State state) const {
    RenderedBoard board;
    std::array<std::array<int, kColumns>, kRows> ids;
    settle(state, board, ids);
    return board;
}

MoveList Game::generate_moves(State state) const {
    RenderedBoard board;
    std::array<std::array<int, kColumns>, kRows> ids;
    settle(state, board, ids);

    MoveList result;
    std::array<bool, kColumns * kRows> visited{};
    std::array<int, kColumns * kRows> queue{};
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kColumns; ++c) {
            const int start = r * kColumns + c;
            if (visited[start] || ids[r][c] < 0) continue;
            Move component{0, r, c, 0, board[r][c]};
            int front = 0;
            int back = 0;
            queue[back++] = start;
            visited[start] = true;
            while (front < back) {
                const int index = queue[front++];
                const int row = index / kColumns;
                const int column = index % kColumns;
                component.mask |= State{1} << ids[row][column];
                ++component.size;
                const int neighbors[4][2] = {
                    {row - 1, column}, {row + 1, column},
                    {row, column - 1}, {row, column + 1}
                };
                for (const auto& neighbor : neighbors) {
                    const int nr = neighbor[0], nc = neighbor[1];
                    if (nr < 0 || nr >= kRows || nc < 0 || nc >= kColumns ||
                        board[nr][nc] != component.color) continue;
                    const int next = nr * kColumns + nc;
                    if (!visited[next]) {
                        visited[next] = true;
                        queue[back++] = next;
                    }
                }
            }
            result.moves[result.count++] = component;
        }
    }
    return result;
}

} // namespace tiles
