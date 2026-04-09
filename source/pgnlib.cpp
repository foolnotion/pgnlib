#include <filesystem>
#include <string_view>
#include <vector>

#include <lexy/action/parse.hpp>
#include <lexy/input/file.hpp>
#include <lexy/input/string_input.hpp>
#include <lexy_ext/report_error.hpp>

#include "grammar.hpp"
#include "pgnlib/pgnlib.hpp"

// ── Eager parsers ─────────────────────────────────────────────────────────────

auto pgn::parse_file(std::filesystem::path const& path)
    -> tl::expected<std::vector<pgn::game>, pgn::parse_error>
{
    auto file = lexy::read_file<lexy::utf8_encoding>(path.c_str());
    if (!file)
        return tl::unexpected(parse_error::file_not_found);

    auto result = lexy::parse<pgn::grammar::pgn_file>(
        file.buffer(), lexy_ext::report_error.path(path.c_str()));

    if (!result.has_value())
        return tl::unexpected(parse_error::syntax_error);

    return std::move(result).value();
}

auto pgn::parse_string(std::string_view input)
    -> tl::expected<std::vector<pgn::game>, pgn::parse_error>
{
    auto in = lexy::string_input<lexy::utf8_encoding>(input.data(), input.size());

    auto result = lexy::parse<pgn::grammar::pgn_file>(in, lexy_ext::report_error);

    if (!result.has_value())
        return tl::unexpected(parse_error::syntax_error);

    return std::move(result).value();
}
