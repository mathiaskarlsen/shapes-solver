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
    // Collision-free identity of the settled visible board, independent of original tile IDs.
    State canonical_key(State state) const;
    // One necessary removal per permanently separated run of a color's columns.
    int column_run_lower_bound(State state) const;

private:
    std::array<char, kColumns * kRows> colors_{};
    std::array<std::array<std::uint16_t, 1 << kRows>, kColumns> canonical_columns_{};
    std::array<std::array<State, kColumns>, 4> color_column_masks_{};
    template<bool NeedIds>
    void settle(State state, RenderedBoard& board,
                std::array<std::array<int, kColumns>, kRows>* ids) const;
    template<bool CollectMoves>
    BoardMetrics scan(State state, MoveList* moves) const;
};

} // namespace tiles
