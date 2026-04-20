#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include "pgnlib/import.hpp"
#include "pgnlib/pgnlib.hpp"

namespace {

std::string read_file(std::filesystem::path const& p)
{
    std::ifstream f(p);
    if (!f.is_open()) {
        throw std::runtime_error("cannot open " + p.string());
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

namespace {

std::string generate_pgn(int n)
{
    std::string out;
    out.reserve(static_cast<std::size_t>(n) * 130);
    for (int i = 1; i <= n; ++i) {
        out += "[Event \"Synthetic\"]\n"
               "[Site \"?\"]\n"
               "[Date \"2024.01.01\"]\n"
               "[Round \"";
        out += std::to_string(i);
        out += "\"]\n"
               "[White \"A\"]\n"
               "[Black \"B\"]\n"
               "[Result \"1-0\"]\n"
               "\n"
               "1. e4 e5 2. Nf3 Nc6 3. Bb5 1-0\n"
               "\n";
    }
    return out;
}

} // namespace

int main()
{
    namespace nb = ankerl::nanobench;

    const std::filesystem::path data_dir{PGNLIB_TEST_DATA_DIR};

    auto const zukertort = read_file(data_dir / "zukertort_steinitz_1886.pgn");

    nb::Bench{}
        .title("parse_string")
        .unit("game")
        .warmup(10)
        .minEpochIterations(500)
        .run("zukertort_steinitz_1886 (38 moves, annotated)", [&] {
            auto result = pgn::parse_string(zukertort);
            nb::doNotOptimizeAway(result);
        });

    nb::Bench{}
        .title("parse_file")
        .unit("game")
        .warmup(5)
        .minEpochIterations(200)
        .run("zukertort_steinitz_1886 (from disk)", [&] {
            auto result = pgn::parse_file(data_dir / "zukertort_steinitz_1886.pgn");
            nb::doNotOptimizeAway(result);
        });

    nb::Bench{}
        .title("game_stream")
        .unit("game")
        .warmup(10)
        .minEpochIterations(500)
        .run("stream_string: zukertort_steinitz_1886", [&] {
            for (auto& eg : pgn::game_stream{std::string_view{zukertort}})
                nb::doNotOptimizeAway(eg.has_value());
        })
        .run("stream_file:   zukertort_steinitz_1886", [&] {
            for (auto& eg : pgn::game_stream{data_dir / "zukertort_steinitz_1886.pgn"})
                nb::doNotOptimizeAway(eg.has_value());
        });

    static constexpr int k100 = 100'000;
    auto const pgn_100k = generate_pgn(k100);

    nb::Bench{}
        .title("import_stream")
        .unit("game")
        .warmup(10)
        .minEpochIterations(500)
        .run("import_stream: zukertort_steinitz_1886 (mainline only)", [&] {
            for (auto& eg : pgn::import_stream{std::string_view{zukertort}})
                nb::doNotOptimizeAway(eg.has_value());
        });

    nb::Bench{}
        .title("throughput")
        .unit("game")
        .batch(k100)
        .warmup(1)
        .minEpochIterations(3)
        .run("game_stream  100K synthetic games (5 half-moves)", [&] {
            for (auto& eg : pgn::game_stream{std::string_view{pgn_100k}})
                nb::doNotOptimizeAway(eg.has_value());
        })
        .run("import_stream 100K synthetic games (5 half-moves)", [&] {
            for (auto& eg : pgn::import_stream{std::string_view{pgn_100k}})
                nb::doNotOptimizeAway(eg.has_value());
        });
}
