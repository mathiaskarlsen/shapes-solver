#include "game.h"
#include "solver.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

void print_usage(std::ostream& output) {
    output << "Usage: solver-bench --random N --seed S [--fast | --time-limit-ms M]\n"
           << "  N: positive number of independent random boards\n"
           << "  S: unsigned 64-bit decimal seed (zero is allowed)\n"
           << "  --fast: measure complete heuristic search without exact proof\n"
           << "  M: per-board exact-search limit in milliseconds (default 1000; 0 means unlimited)\n";
}

std::uint64_t parse_unsigned(std::string_view value, std::string_view option) {
    std::uint64_t number = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size()) {
        throw std::invalid_argument(std::string(option) + " expects an unsigned decimal integer: '" +
                                    std::string(value) + "'");
    }
    return number;
}

// A nearest-rank percentile is the observation at rank ceil(percent * count / 100).
std::size_t percentile_index(std::size_t count, std::size_t percent) {
    const std::size_t rank = (count / 100) * percent +
                             ((count % 100) * percent + 99) / 100;
    return rank - 1;
}

template <typename T>
void print_distribution(std::string_view name, std::vector<T>& samples,
                        std::string_view max_label = "max") {
    std::sort(samples.begin(), samples.end());
    const std::size_t count = samples.size();
    const long double median = count % 2 != 0
                                   ? static_cast<long double>(samples[count / 2])
                                   : (static_cast<long double>(samples[count / 2 - 1]) +
                                      static_cast<long double>(samples[count / 2])) / 2;
    std::cout << name << ": min=" << samples.front() << " median=" << median
              << " p95=" << samples[percentile_index(count, 95)]
              << " p99=" << samples[percentile_index(count, 99)]
              << ' ' << max_label << '=' << samples.back() << '\n';
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        print_usage(std::cout);
        return 0;
    }

    try {
        std::optional<std::uint64_t> requested_count;
        std::optional<std::uint64_t> seed;
        std::uint64_t limit_ms = 1000;
        bool limit_set = false;
        bool fast = false;

        for (int index = 1; index < argc;) {
            const std::string_view option(argv[index++]);
            if (option == "--fast") {
                if (fast) throw std::invalid_argument("--fast specified more than once");
                fast = true;
                continue;
            }
            if (option != "--random" && option != "--seed" && option != "--time-limit-ms") {
                throw std::invalid_argument("unknown option: " + std::string(option));
            }
            if (index >= argc) throw std::invalid_argument("missing value for " + std::string(option));
            const std::uint64_t value = parse_unsigned(argv[index++], option);
            if (option == "--random") {
                if (requested_count) {
                    throw std::invalid_argument("--random specified more than once");
                }
                requested_count = value;
            } else if (option == "--seed") {
                if (seed) {
                    throw std::invalid_argument("--seed specified more than once");
                }
                seed = value;
            } else {
                if (limit_set) {
                    throw std::invalid_argument("--time-limit-ms specified more than once");
                }
                limit_set = true;
                limit_ms = value;
            }
        }
        if (fast && limit_set) {
            throw std::invalid_argument("--fast and --time-limit-ms cannot be combined");
        }

        if (!requested_count || !seed) {
            throw std::invalid_argument("both --random N and --seed S are required");
        }
        if (*requested_count == 0) {
            throw std::invalid_argument("--random must be greater than zero");
        }
        if (*requested_count > std::numeric_limits<std::size_t>::max()) {
            throw std::invalid_argument("--random exceeds this platform's board count range");
        }
        if (limit_ms > static_cast<std::uint64_t>(
                           std::numeric_limits<std::chrono::milliseconds::rep>::max())) {
            throw std::invalid_argument("--time-limit-ms exceeds this platform's limit range");
        }

        const auto count = static_cast<std::size_t>(*requested_count);
        const auto time_limit = std::chrono::milliseconds{
            static_cast<std::chrono::milliseconds::rep>(limit_ms)};
        std::mt19937_64 random(*seed);
        constexpr char colors[] = "PBGO";
        std::vector<double> times_ms;
        std::vector<std::size_t> lengths;
        std::vector<std::uint64_t> states;
        std::vector<std::uint64_t> hits;
        std::vector<int> depths;
        times_ms.reserve(count);
        lengths.reserve(count);
        states.reserve(count);
        hits.reserve(count);
        depths.reserve(count);

        std::cout << "boards=" << count << " seed=" << *seed
                  << " mode=" << (fast ? "fast" : "exact")
                  << " time_limit_ms=" << (fast ? 0 : limit_ms) << " (0=unlimited)\n"
                  << "generator=mt19937_64, two low bits per tile in row-major order; "
                     "colors=PBGO\n"
                  << "board time_ms states_expanded transposition_hits max_search_depth moves "
                     "optimal_proven\n"
                  << std::fixed << std::setprecision(3);

        std::size_t proven = 0;
        for (std::size_t board_index = 0; board_index < count; ++board_index) {
            std::array<std::string, tiles::kRows> rows;
            for (auto& row : rows) {
                row.resize(tiles::kColumns);
                for (char& tile : row) {
                    tile = colors[random() & 3ULL];
                }
            }
            const tiles::Game game(rows);
            const auto solution = tiles::Solver(game).solve(
                !fast, fast ? std::chrono::milliseconds{0} : time_limit);
            const double time_ms = solution.elapsed_seconds * 1000.0;
            times_ms.push_back(time_ms);
            lengths.push_back(solution.moves.size());
            states.push_back(solution.states_expanded);
            hits.push_back(solution.transposition_hits);
            depths.push_back(solution.max_search_depth);
            if (solution.optimal) ++proven;

            std::cout << (board_index + 1) << ' ' << time_ms << ' '
                      << solution.states_expanded << ' ' << solution.transposition_hits << ' '
                      << solution.max_search_depth << ' ' << solution.moves.size() << ' '
                      << (solution.optimal ? "yes" : "no") << '\n';
        }

        std::cout << "Summary (all boards, including unproven results; p95/p99 use nearest rank):\n"
                  << "proven=" << proven << " not_proven=" << (count - proven) << '\n';
        print_distribution("time_ms", times_ms, "worst_case");
        print_distribution("moves", lengths);
        print_distribution("states_expanded", states);
        print_distribution("transposition_hits", hits);
        print_distribution("max_search_depth", depths);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "solver-bench: " << error.what() << '\n';
        print_usage(std::cerr);
        return 1;
    }
}
