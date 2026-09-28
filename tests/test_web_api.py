"""Exercise real fast-search JSON steps, including original IDs and gravity."""

import json
import subprocess
import sys
from pathlib import Path


ROWS = 9
COLUMNS = 7
EXECUTABLE = Path(sys.argv[1])


def solve(board):
    result = subprocess.run(
        [str(EXECUTABLE)], input="\n".join(board) + "\n", text=True,
        capture_output=True, check=True, timeout=30,
    )
    return json.loads(result.stdout)


def settled_positions(alive):
    positions = {}
    for column in range(COLUMNS):
        bottom = ROWS - 1
        for row in range(ROWS - 1, -1, -1):
            tile = row * COLUMNS + column
            if tile in alive:
                positions[tile] = bottom
                bottom -= 1
    return positions


def check_path(board):
    original_colors = {
        row * COLUMNS + column: color
        for row, line in enumerate(board)
        for column, color in enumerate(line)
        if color != "."
    }
    result = solve(board)
    positions = {tile["id"]: tile["row"] for tile in result["initial"]}
    assert positions == settled_positions(original_colors), "initial board did not settle"
    alive = set(original_colors)
    assert result["length"] == len(result["steps"]), "step count mismatch"

    for step in result["steps"]:
        visible = {(row, tile % COLUMNS): tile for tile, row in positions.items()}
        clicked = (step["row"], step["column"])
        assert clicked in visible, "click is not on a surviving tile"
        color = original_colors[visible[clicked]]
        assert step["color"] == color, "click color mismatch"
        frontier = [clicked]
        component = {clicked}
        for row, column in frontier:
            for neighbor in ((row - 1, column), (row + 1, column),
                             (row, column - 1), (row, column + 1)):
                tile = visible.get(neighbor)
                if (tile is not None and neighbor not in component and
                        original_colors[tile] == color):
                    component.add(neighbor)
                    frontier.append(neighbor)
        removed = {visible[cell] for cell in component}
        assert len(removed) == step["size"], "wrong component size"
        assert removed == set(step["removed"]), "move omitted or added tiles"
        alive -= removed
        positions = {tile["id"]: tile["row"] for tile in step["positions"]}
        assert positions == settled_positions(alive), "post-click gravity/order mismatch"

    assert not alive, "fast solution did not clear the board"
    return result


def main():
    empty = ["." * COLUMNS for _ in range(ROWS)]
    assert check_path(empty)["length"] == 0

    sparse = [list(row) for row in empty]
    for row, column, color in ((0, 0, "P"), (1, 0, "G"), (2, 0, "P"),
                               (7, 1, "P"), (8, 1, "B")):
        sparse[row][column] = color
    sparse = ["".join(row) for row in sparse]
    sparse_result = check_path(sparse)
    assert {entry["id"]: entry["row"] for entry in sparse_result["initial"]}[0] == 6

    example = Path(__file__).resolve().parents[1] / "examples" / "initial-board.txt"
    full = example.read_text(encoding="utf-8").splitlines()
    assert check_path(full)["length"] <= 12

    invalid = subprocess.run(
        [str(EXECUTABLE)], input=("X" * COLUMNS + "\n") * ROWS,
        text=True, capture_output=True, timeout=30,
    )
    assert invalid.returncode != 0, "invalid colors were accepted"


if __name__ == "__main__":
    main()
