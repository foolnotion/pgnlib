#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

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
}
