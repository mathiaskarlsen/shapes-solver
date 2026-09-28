#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace tiles {

constexpr int kColumns = 7;
constexpr int kRows = 9;
using State = std::uint64_t;
constexpr State kFullBoard = (State{1} << (kColumns * kRows)) - 1;

struct Move {
    State mask = 0; // IDs of original tiles removed by this component
    int row = 0;    // 0-based, top of the CURRENT settled board
    int column = 0; // 0-based, left to right
    int size = 0;
    char color = '.';
};

struct MoveList {
    std::array<Move, kColumns * kRows> moves{};
    int count = 0;
    const Move* begin() const { return moves.data(); }
    const Move* end() const { return moves.data() + count; }
};

struct BoardMetrics {
    int components = 0;
    int largest = 0;
    int distinct_colors = 0;
};

using RenderedBoard = std::array<std::array<char, kColumns>, kRows>;

class Game {
public:
    explicit Game(const std::array<std::string, kRows>& rows);
    static Game from_file(const std::string& path);

    State initial_state() const { return kFullBoard; }
    MoveList generate_moves(State state) const;
    BoardMetrics measure(State state) const;
    State apply_move(State state, const Move& move) const { return state & ~move.mask; }
    bool is_empty(State state) const { return state == 0; }
    RenderedBoard render(State state) const;

private:
    std::array<char, kColumns * kRows> colors_{};
    template<bool NeedIds>
    void settle(State state, RenderedBoard& board,
                std::array<std::array<int, kColumns>, kRows>* ids) const;
    template<bool CollectMoves>
    BoardMetrics scan(State state, MoveList* moves) const;
};

} // namespace tiles
