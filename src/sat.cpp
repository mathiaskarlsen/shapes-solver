#include "sat.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace tiles {
namespace {
constexpr int kCells = kColumns * kRows;
constexpr std::array<char, 4> kColors{'P', 'B', 'G', 'O'};
using CellColors = std::array<int, 4>;

// Cell index is height from the bottom, then column. Gravity preserves this
// order, and the displayed row is kRows - 1 - height.
constexpr int cell(int height, int column) { return height * kColumns + column; }

struct Frame {
    std::array<CellColors, kCells> color{};
    std::array<int, kCells> empty{};
    std::array<int, kCells> click{};
    std::array<int, kCells> removed{};
    std::array<int, kCells> keep{};
    std::array<std::array<std::array<int, kRows + 1>, kRows + 1>, kColumns> prefix{};
    std::array<std::array<int, kCells>, kCells> reach{};
    std::array<int, 4> chosen{};
    int active = 0;
};

class Cnf {
public:
    Cnf(std::ostream& output, std::uint64_t variable_limit, std::uint64_t clause_limit)
        : output_(output), variable_limit_(variable_limit), clause_limit_(clause_limit) {}

    int variable() {
        if (variables_ >= variable_limit_ || variables_ >= std::numeric_limits<int>::max()) {
            limited_ = true;
            return 0;
        }
        return static_cast<int>(++variables_);
    }

    void clause(std::initializer_list<int> literals) { clause(literals.begin(), literals.size()); }
    void clause(const int* literals, std::size_t count) {
        if (limited_) return;
        if (clauses_ >= clause_limit_) {
            limited_ = true;
            return;
        }
        for (std::size_t i = 0; i < count; ++i) output_ << literals[i] << ' ';
        output_ << "0\n";
        ++clauses_;
    }

    void equivalence(int out, int a) { clause({-out, a}); clause({out, -a}); }
    std::uint64_t variables() const { return variables_; }
    std::uint64_t clauses() const { return clauses_; }
    bool limited() const { return limited_; }
    bool good() const { return output_.good(); }

private:
    std::ostream& output_;
    std::uint64_t variable_limit_;
    std::uint64_t clause_limit_;
    std::uint64_t variables_ = 0;
    std::uint64_t clauses_ = 0;
    bool limited_ = false;
};

void allocate_board(Cnf& cnf, Frame& frame) {
    for (int i = 0; i < kCells; ++i) {
        frame.empty[i] = cnf.variable();
        for (int color = 0; color < 4; ++color) frame.color[i][color] = cnf.variable();
    }
}

void exactly_one_board(Cnf& cnf, const Frame& frame) {
    for (int i = 0; i < kCells; ++i) {
        std::array<int, 5> choices{frame.empty[i], frame.color[i][0], frame.color[i][1],
                                   frame.color[i][2], frame.color[i][3]};
        cnf.clause(choices.data(), choices.size());
        for (int a = 0; a < 5; ++a)
            for (int b = a + 1; b < 5; ++b) cnf.clause({-choices[a], -choices[b]});
    }
}

void encode_step(Cnf& cnf, Frame& current, const Frame& next) {
    current.active = cnf.variable();
    for (int k = 0; k < 4; ++k) {
        current.chosen[k] = cnf.variable();
        cnf.clause({-current.chosen[k], current.active});
    }
    cnf.clause({-current.active, current.chosen[0], current.chosen[1],
                current.chosen[2], current.chosen[3]});
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b)
            cnf.clause({-current.chosen[a], -current.chosen[b]});

    std::array<int, kCells + 1> all_clicks{};
    all_clicks[0] = -current.active;
    std::array<int, kCells + 1> all_occupied{};
    all_occupied[0] = -current.active;
    for (int i = 0; i < kCells; ++i) {
        current.click[i] = cnf.variable();
        current.removed[i] = cnf.variable();
        current.keep[i] = cnf.variable();
        all_clicks[i + 1] = current.click[i];
        all_occupied[i + 1] = -current.empty[i];
        cnf.clause({current.empty[i], current.active});
        cnf.clause({-current.click[i], current.active});
        cnf.clause({-current.click[i], current.removed[i]});
        cnf.clause({-current.click[i], -current.empty[i]});
        cnf.clause({-current.removed[i], -current.empty[i]});
        // keep iff occupied and not removed.
        cnf.clause({-current.keep[i], -current.empty[i]});
        cnf.clause({-current.keep[i], -current.removed[i]});
        cnf.clause({current.empty[i], current.removed[i], current.keep[i]});
        for (int k = 0; k < 4; ++k) {
            cnf.clause({-current.click[i], -current.color[i][k], current.chosen[k]});
            cnf.clause({-current.removed[i], -current.chosen[k], current.color[i][k]});
        }
    }
    cnf.clause(all_clicks.data(), all_clicks.size());
    cnf.clause(all_occupied.data(), all_occupied.size());
    for (int a = 0; a < kCells; ++a)
        for (int b = a + 1; b < kCells; ++b)
            cnf.clause({-current.click[a], -current.click[b]});

    // Closure + matching color forces all neighboring tiles of the clicked
    // color into the removal set. A bounded predecessor chain forces every
    // removed tile to actually be reachable from the click, excluding remote
    // same-color components. This includes singleton components.
    for (int i = 0; i < kCells; ++i) current.reach[0][i] = current.click[i];
    for (int depth = 1; depth < kCells; ++depth)
        for (int i = 0; i < kCells; ++i)
            current.reach[depth][i] = cnf.variable();
    for (int i = 0; i < kCells; ++i) {
        const int h = i / kColumns;
        const int c = i % kColumns;
        std::array<int, 4> neighbors{};
        int count = 0;
        if (h > 0) neighbors[count++] = cell(h - 1, c);
        if (h + 1 < kRows) neighbors[count++] = cell(h + 1, c);
        if (c > 0) neighbors[count++] = cell(h, c - 1);
        if (c + 1 < kColumns) neighbors[count++] = cell(h, c + 1);
        for (int n = 0; n < count; ++n)
            for (int k = 0; k < 4; ++k)
                cnf.clause({-current.removed[i], -current.chosen[k],
                            -current.color[neighbors[n]][k], current.removed[neighbors[n]]});
        for (int depth = 1; depth < kCells; ++depth) {
            const int reachable = current.reach[depth][i];
            cnf.clause({-reachable, current.removed[i]});
            cnf.clause({-current.reach[depth - 1][i], reachable});
            std::array<int, 6> predecessors{};
            predecessors[0] = -reachable;
            predecessors[1] = current.reach[depth - 1][i];
            for (int n = 0; n < count; ++n)
                predecessors[n + 2] = current.reach[depth - 1][neighbors[n]];
            cnf.clause(predecessors.data(), static_cast<std::size_t>(count + 2));
        }
        cnf.clause({-current.removed[i], current.reach[kCells - 1][i]});
    }

    // Unary prefix survivor counts per column. prefix[c][i][j] means at
    // least j of the bottom i original cells survive this click. Every old
    // survivor maps to exactly its rank among bottom-up survivors, and no
    // color can move horizontally or overtake another survivor.
    for (int c = 0; c < kColumns; ++c) {
        for (int i = 1; i <= kRows; ++i) {
            const int keep = current.keep[cell(i - 1, c)];
            for (int j = 1; j <= i; ++j) {
                const int out = current.prefix[c][i][j] = cnf.variable();
                if (j == 1) {
                    if (i == 1) cnf.equivalence(out, keep);
                    else {
                        const int a = current.prefix[c][i - 1][1];
                        cnf.clause({-a, out}); cnf.clause({-keep, out});
                        cnf.clause({-out, a, keep});
                    }
                } else if (j == i) {
                    const int a = current.prefix[c][i - 1][j - 1];
                    cnf.clause({-out, a}); cnf.clause({-out, keep});
                    cnf.clause({out, -a, -keep});
                } else {
                    const int a = current.prefix[c][i - 1][j];
                    const int b = current.prefix[c][i - 1][j - 1];
                    cnf.clause({-a, out}); cnf.clause({-b, -keep, out});
                    cnf.clause({-out, a, b}); cnf.clause({-out, a, keep});
                }
            }
        }
        for (int height = 0; height < kRows; ++height) {
            // Exact occupancy of each next cell follows the final count.
            cnf.equivalence(next.empty[cell(height, c)],
                            -current.prefix[c][kRows][height + 1]);
            for (int old_height = height; old_height < kRows; ++old_height) {
                const int old_cell = cell(old_height, c);
                const int lower = height == 0 ? 0 : current.prefix[c][old_height][height];
                const int higher = height == old_height ? 0
                                   : current.prefix[c][old_height][height + 1];
                for (int k = 0; k < 4; ++k) {
                    std::array<int, 5> implication{};
                    int count = 0;
                    implication[count++] = -current.keep[old_cell];
                    implication[count++] = -current.color[old_cell][k];
                    if (lower) implication[count++] = -lower;
                    if (higher) implication[count++] = higher;
                    implication[count++] = next.color[cell(height, c)][k];
                    cnf.clause(implication.data(), static_cast<std::size_t>(count));
                }
            }
        }
    }
}

// MiniSat writes SAT/UNSAT plus signed DIMACS literals to a separate file.
// A SAT result is not accepted without a syntactically complete assignment.
int invoke_minisat(const std::string& executable, const std::string& input,
                   const std::string& model) {
    // An optional Python MiniSat-protocol wrapper allows local validation
    // without installing a solver binary. The normal path uses MiniSat directly.
    const bool script = std::filesystem::path(executable).extension() == ".py";
    const char* python = std::getenv("PYTHON");
    if (!python || !*python) python = "python";
#ifdef _WIN32
    if (script) {
        return static_cast<int>(_spawnlp(_P_WAIT, python, python, executable.c_str(),
                                         input.c_str(), model.c_str(), nullptr));
    }
    return static_cast<int>(_spawnlp(_P_WAIT, executable.c_str(), executable.c_str(),
                                     input.c_str(), model.c_str(), nullptr));
#else
    const pid_t pid = fork();
    if (pid == 0) {
        if (script) {
            execlp(python, python, executable.c_str(), input.c_str(), model.c_str(),
                   static_cast<char*>(nullptr));
        } else {
            execlp(executable.c_str(), executable.c_str(), input.c_str(), model.c_str(),
                   static_cast<char*>(nullptr));
        }
        _exit(127);
    }
    if (pid < 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

struct TemporaryModel {
    std::string path;
    ~TemporaryModel() {
        if (path.empty()) return;
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

} // namespace

SatResult solve_sat_bounded(const Game& game, State initial, int target_moves,
                            const SatOptions& options) {
    SatResult result;
    if (target_moves < 0 || target_moves > kCells || options.cnf_path.empty()) {
        result.detail = "target must be 0..63 and a CNF output path is required";
        return result;
    }
    if (initial & ~kFullBoard) {
        result.detail = "initial state contains a tile ID outside the board";
        return result;
    }
    // Reject before creating an output file when a requested proof exceeds a
    // conservative construction budget. The writer also checks exact counts.
    const auto horizon = static_cast<std::uint64_t>(target_moves);
    if (options.max_variables < 315 + 5'000 * horizon ||
        options.max_clauses < 700 + 100'000 * horizon) {
        result.status = SatStatus::resource_limit;
        result.detail = "CNF construction exceeds configured budget estimate";
        return result;
    }
    std::fstream cnf(options.cnf_path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!cnf) {
        result.detail = "cannot create CNF output file";
        return result;
    }
    // Fixed-width fields let us stream clauses instead of holding the entire
    // multi-megabyte CNF in memory. On a failed budget, the partial file is
    // deleted rather than being mistaken for a valid proof input.
    cnf << "p cnf " << std::setw(20) << 0 << ' ' << std::setw(20) << 0 << '\n';
    Cnf encoder(cnf, options.max_variables, options.max_clauses);
    std::vector<Frame> frames(static_cast<std::size_t>(target_moves) + 1);
    for (auto& frame : frames) allocate_board(encoder, frame);
    const auto rendered = game.render(initial);
    for (int t = 0; t <= target_moves; ++t) {
        exactly_one_board(encoder, frames[t]);
        if (t == 0) {
            for (int i = 0; i < kCells; ++i) {
                const char color = rendered[kRows - 1 - i / kColumns][i % kColumns];
                if (color == '.') encoder.clause({frames[0].empty[i]});
                else {
                    int k = 0;
                    while (k < 4 && kColors[k] != color) ++k;
                    if (k == 4) {
                        cnf.close();
                        std::filesystem::remove(options.cnf_path);
                        result.detail = "initial board has a color outside P/B/G/O";
                        return result;
                    }
                    encoder.clause({frames[0].color[i][k]});
                }
            }
        }
    }
    for (int t = 0; t < target_moves && !encoder.limited() && encoder.good(); ++t)
        encode_step(encoder, frames[t], frames[t + 1]);
    for (int i = 0; i < kCells; ++i) encoder.clause({frames.back().empty[i]});
    result.variables = encoder.variables();
    result.clauses = encoder.clauses();
    if (encoder.limited() || !encoder.good()) {
        cnf.close();
        std::filesystem::remove(options.cnf_path);
        result.status = encoder.limited() ? SatStatus::resource_limit : SatStatus::unknown;
        result.detail = encoder.limited() ? "CNF construction exceeded configured budget"
                                          : "failed writing CNF";
        return result;
    }
    cnf.seekp(0);
    cnf << "p cnf " << std::setw(20) << result.variables << ' '
        << std::setw(20) << result.clauses << '\n';
    cnf.close();
    if (!cnf) {
        std::filesystem::remove(options.cnf_path);
        result.detail = "failed completing CNF output file";
        return result;
    }
    if (options.solver_path.empty()) {
        result.detail = "CNF exported; no external solver configured";
        return result;
    }

    // Never delete a pre-existing user file merely to avoid accepting a stale model.
    TemporaryModel model_file{options.cnf_path + "." + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + ".model"};
    std::error_code error;
    if (std::filesystem::exists(model_file.path, error) || error) {
        model_file.path.clear();
        result.detail = "temporary solver model path already exists or cannot be checked";
        return result;
    }
    const int exit_code = invoke_minisat(options.solver_path, options.cnf_path, model_file.path);
    if (exit_code != 10 && exit_code != 20) {
        result.detail = "solver unavailable, interrupted, or returned an unrecognized status";
        return result;
    }
    std::ifstream model(model_file.path);
    std::string status;
    if (!(model >> status) || (exit_code == 10 && status != "SAT") ||
        (exit_code == 20 && status != "UNSAT")) {
        result.detail = "solver status and MiniSat model file disagree";
        return result;
    }
    if (exit_code == 20) {
        result.status = SatStatus::proven_impossible;
        result.detail = "MiniSat returned UNSAT for the complete bounded encoding";
        return result;
    }
    std::vector<std::int8_t> assignment(static_cast<std::size_t>(result.variables) + 1, -1);
    long long literal = 0;
    while (model >> literal) {
        if (literal == 0) break;
        if (literal == std::numeric_limits<long long>::min() ||
            static_cast<std::uint64_t>(literal < 0 ? -literal : literal) > result.variables) {
            result.detail = "solver model contains an invalid variable";
            return result;
        }
        const auto id = static_cast<std::size_t>(literal < 0 ? -literal : literal);
        const std::int8_t value = literal > 0 ? 1 : 0;
        if (assignment[id] != -1 && assignment[id] != value) {
            result.detail = "solver model contains contradictory literals";
            return result;
        }
        assignment[id] = value;
    }
    if (!model || literal != 0) {
        result.detail = "solver model is incomplete or malformed";
        return result;
    }
    for (std::size_t id = 1; id < assignment.size(); ++id) {
        if (assignment[id] == -1) {
            result.detail = "solver model does not assign every variable";
            return result;
        }
    }
    State state = initial;
    for (int t = 0; t < target_moves && !game.is_empty(state); ++t) {
        int chosen = -1;
        for (int i = 0; i < kCells; ++i) {
            if (assignment[frames[t].click[i]] != 1) continue;
            if (chosen != -1) {
                result.detail = "solver model clicks more than one tile";
                result.moves.clear();
                return result;
            }
            chosen = i;
        }
        if (chosen == -1) {
            result.detail = "solver model omitted a required click";
            result.moves.clear();
            return result;
        }
        const int row = kRows - 1 - chosen / kColumns;
        const int column = chosen % kColumns;
        const auto board = game.render(state);
        const char clicked_color = board[row][column];
        std::array<int, kCells> queue{};
        std::array<bool, kCells> visited{};
        int first = 0, last = 0;
        const int start = row * kColumns + column;
        queue[last++] = start;
        visited[start] = true;
        while (first != last) {
            const int at = queue[first++];
            const int r = at / kColumns, c = at % kColumns;
            const std::array<int, 4> neighbors{r ? at - kColumns : -1,
                                                r + 1 < kRows ? at + kColumns : -1,
                                                c ? at - 1 : -1,
                                                c + 1 < kColumns ? at + 1 : -1};
            for (int n : neighbors) {
                if (n < 0 || visited[n] || board[n / kColumns][n % kColumns] != clicked_color)
                    continue;
                visited[n] = true;
                queue[last++] = n;
            }
        }
        bool matched = false;
        for (const Move& move : game.generate_moves(state)) {
            if (!visited[move.row * kColumns + move.column]) continue;
            result.moves.push_back(move);
            state = game.apply_move(state, move);
            matched = true;
            break;
        }
        if (!matched) {
            result.moves.clear();
            result.detail = "solver click cannot be replayed by Game";
            return result;
        }
    }
    if (!game.is_empty(state)) {
        result.moves.clear();
        result.detail = "solver path did not clear the board within the horizon";
        return result;
    }
    result.status = SatStatus::found;
    result.detail = "SAT model decoded and replayed to the empty board";
    return result;
}

} // namespace tiles
