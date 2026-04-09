#ifndef PGNLIB_PGNLIB_HPP
#define PGNLIB_PGNLIB_HPP

#include <filesystem>
#include <string_view>
#include <vector>

#include <tl/expected.hpp>

#include "pgnlib/pgnlib_export.hpp"
#include "pgnlib/types.hpp"

namespace pgn {

enum class parse_error : u8 {
    file_not_found,
    syntax_error,
};

// ── Eager parsers ─────────────────────────────────────────────────────────────

PGNLIB_EXPORT auto parse_file(std::filesystem::path const& path)
    -> tl::expected<std::vector<game>, parse_error>;

PGNLIB_EXPORT auto parse_string(std::string_view input)
    -> tl::expected<std::vector<game>, parse_error>;

} // namespace pgn

#endif // PGNLIB_PGNLIB_HPP
