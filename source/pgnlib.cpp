#include <filesystem>
#include <string_view>
#include <vector>

#include "pgnlib/pgnlib.hpp"

auto pgn::parse_file(std::filesystem::path const& /*path*/)
    -> tl::expected<std::vector<pgn::game>, pgn::parse_error>
{
    return tl::unexpected(parse_error::syntax_error);
}

auto pgn::parse_string(std::string_view /*input*/)
    -> tl::expected<std::vector<pgn::game>, pgn::parse_error>
{
    return tl::unexpected(parse_error::syntax_error);
}
