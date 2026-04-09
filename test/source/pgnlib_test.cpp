#include <catch2/catch_test_macros.hpp>

#include "pgnlib/pgnlib.hpp"

TEST_CASE("library links and headers compile", "[smoke]")
{
    auto result = pgn::parse_string("");
    CHECK_FALSE(result.has_value());
}
