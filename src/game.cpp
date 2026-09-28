#include "game.h"

#include <algorithm>
#include <bit>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace tiles {
namespace {
constexpr auto kNeighbors = [] {
    std::array<std::array<int, 4>, kColumns * kRows> neighbors{};
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kColumns; ++c) {
            const int index = r * kColumns + c;
            neighbors[index] = {r ? index - kColumns : -1,
                                r + 1 < kRows ? index + kColumns : -1,
                                c ? index - 1 : -1,
                                c + 1 < kColumns ? index + 1 : -1};
        }
    }
    return neighbors;
}();
} // namespace

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
            const int color_index = ch == 'P' ? 0 : ch == 'B' ? 1 : ch == 'G' ? 2 : 3;
            color_column_masks_[color_index][c] |= State{1} << (r * kColumns + c);
        }
    }
    for (int c = 0; c < kColumns; ++c) {
        std::array<std::pair<std::uint32_t, std::uint16_t>, 1 << kRows> sequences{};
        for (int mask = 0; mask < (1 << kRows); ++mask) {
            std::uint32_t sequence = 1; // A leading base-5 digit distinguishes lengths.
            for (int r = 0; r < kRows; ++r) {
                if (!(mask & (1 << r))) continue;
                const char color = colors_[r * kColumns + c];
                const unsigned digit = color == 'P' ? 1 : color == 'B' ? 2 : color == 'G' ? 3 : 4;
                sequence = sequence * 5 + digit;
            }
            sequences[mask] = {sequence, static_cast<std::uint16_t>(mask)};
        }
        std::sort(sequences.begin(), sequences.end());
        std::uint16_t representative = 0;
        std::uint32_t previous = 0;
        for (const auto& [sequence, mask] : sequences) {
            if (sequence != previous) representative = mask;
            canonical_columns_[c][mask] = representative;
            previous = sequence;
        }
    }
}

State Game::canonical_key(State state) const {
    State key = 0;
    for (int c = 0; c < kColumns; ++c) {
        unsigned local = 0;
        for (int r = 0; r < kRows; ++r) {
            local |= static_cast<unsigned>((state >> (r * kColumns + c)) & 1) << r;
        }
        key |= State{canonical_columns_[c][local]} << (c * kRows);
    }
    return key;
}

int Game::column_run_lower_bound(State state) const {
    int runs = 0;
    for (const auto& color : color_column_masks_) {
        unsigned columns = 0;
        for (int c = 0; c < kColumns; ++c) {
            columns |= static_cast<unsigned>((state & color[c]) != 0) << c;
        }
        runs += std::popcount(columns & ~(columns << 1));
    }
    return runs;
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

template<bool NeedIds>
void Game::settle(State state, RenderedBoard& board,
                  std::array<std::array<int, kColumns>, kRows>* ids) const {
    for (auto& row : board) row.fill('.');
    for (int c = 0; c < kColumns; ++c) {
        int bottom = kRows - 1;
        for (int r = kRows - 1; r >= 0; --r) {
            const int id = r * kColumns + c;
            if (state & (State{1} << id)) {
                board[bottom][c] = colors_[id];
                if constexpr (NeedIds) (*ids)[bottom][c] = id;
                --bottom;
            }
        }
    }
}

RenderedBoard Game::render(State state) const {
    RenderedBoard board;
    settle<false>(state, board, nullptr);
    return board;
}

template<bool CollectMoves>
BoardMetrics Game::scan(State state, MoveList* moves) const {
    RenderedBoard board;
    std::array<std::array<int, kColumns>, kRows> ids;
    settle<CollectMoves>(state, board, &ids);

    BoardMetrics metrics;
    unsigned colors = 0;
    std::array<bool, kColumns * kRows> visited{};
    std::array<int, kColumns * kRows> queue{};
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kColumns; ++c) {
            const int start = r * kColumns + c;
            const char color = board[r][c];
            if (visited[start] || color == '.') continue;
            switch (color) {
            case 'P': colors |= 1; break;
            case 'B': colors |= 2; break;
            case 'G': colors |= 4; break;
            default: colors |= 8; break;
            }
            Move component;
            if constexpr (CollectMoves) component = {0, r, c, 0, color};
            int size = 0;
            int front = 0;
            int back = 0;
            queue[back++] = start;
            visited[start] = true;
            while (front < back) {
                const int index = queue[front++];
                const int row = index / kColumns;
                const int column = index % kColumns;
                if constexpr (CollectMoves) component.mask |= State{1} << ids[row][column];
                ++size;
                for (int next : kNeighbors[index]) {
                    if (next < 0 || visited[next]) continue;
                    if (board[next / kColumns][next % kColumns] == color) {
                        visited[next] = true;
                        queue[back++] = next;
                    }
                }
            }
            ++metrics.components;
            if (size > metrics.largest) metrics.largest = size;
            if constexpr (CollectMoves) {
                component.size = size;
                moves->moves[moves->count++] = component;
            }
        }
    }
    metrics.distinct_colors = std::popcount(colors);
    return metrics;
}

MoveList Game::generate_moves(State state) const {
    MoveList moves;
    scan<true>(state, &moves);
    return moves;
}

BoardMetrics Game::measure(State state) const {
    return scan<false>(state, nullptr);
}

} // namespace tiles
