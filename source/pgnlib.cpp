#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <lexy/action/parse.hpp>
#include <lexy/input/file.hpp>
#include <lexy/input/string_input.hpp>
#include <lexy_ext/report_error.hpp>

#include "grammar.hpp"
#include "pgnlib/pgnlib.hpp"

// ── Internal helper ───────────────────────────────────────────────────────────

static auto parse_one_game(std::string_view slice)
    -> tl::expected<pgn::game, pgn::parse_error>
{
    auto in = lexy::string_input<lexy::utf8_encoding>(slice.data(), slice.size());
    auto result = lexy::parse<pgn::grammar::game_rule>(in, lexy_ext::report_error);
    if (!result.has_value())
        return tl::unexpected(pgn::parse_error::syntax_error);
    return std::move(result).value();
}

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

// ── game_stream ───────────────────────────────────────────────────────────────

struct pgn::game_stream::impl {
    std::string      owned_buf;       // non-empty when we own the buffer (file ctor)
    std::string_view src;             // entire source — never advanced (for offset calc)
    std::string_view remaining;       // current parse position
    std::size_t      current_offset{0}; // byte offset of the current game's '['
    tl::expected<game, parse_error> current;
    bool done = false;

    // Split off the text of the first game from sv.
    // Returns {game_slice, rest_starting_at_next_'['}.
    //
    // Boundary heuristic: a blank line followed by '[' is the standard PGN
    // inter-game separator.  Handles LF (\n\n), CRLF (\r\n\r\n), and mixed
    // line endings by treating any \n (optionally preceded by \r) as a line
    // boundary.
    static auto split_next(std::string_view sv)
        -> std::pair<std::string_view, std::string_view>
    {
        std::size_t pos = 0;
        while (pos < sv.size()) {
            auto nl = sv.find('\n', pos);
            if (nl == std::string_view::npos)
                return {sv, {}};

            // Check for a blank line: \n followed by optional \r then \n
            std::size_t next = nl + 1;
            if (next < sv.size() && sv[next] == '\r')
                ++next;                         // absorb the \r in \r\n\r\n

            if (next < sv.size() && sv[next] == '\n') {
                // Blank line confirmed — skip any further blank lines
                std::size_t rest = next + 1;
                while (rest < sv.size() && (sv[rest] == '\r' || sv[rest] == '\n'))
                    ++rest;

                if (rest < sv.size() && sv[rest] == '[')
                    return {sv.substr(0, next + 1), sv.substr(rest)};
            }

            pos = nl + 1;
        }
        return {sv, {}};
    }

    void advance() {
        // skip leading whitespace, blank lines, and %-line comments
        auto pos = remaining.find_first_not_of(" \t\r\n");
        while (pos != std::string_view::npos && remaining[pos] == '%') {
            auto eol = remaining.find('\n', pos);
            if (eol == std::string_view::npos) { done = true; return; }
            remaining = remaining.substr(eol + 1);
            pos = remaining.find_first_not_of(" \t\r\n");
        }
        if (pos == std::string_view::npos || remaining[pos] != '[') {
            done = true;
            return;
        }
        remaining      = remaining.substr(pos);
        current_offset = static_cast<std::size_t>(remaining.data() - src.data());

        auto [slice, rest] = split_next(remaining);
        remaining = rest;
        current   = parse_one_game(slice);
    }
};

// ── Constructors / destructor ─────────────────────────────────────────────────

pgn::game_stream::game_stream(std::filesystem::path const& path)
    : impl_(std::make_unique<impl>())
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        impl_->current = tl::unexpected(parse_error::file_not_found);
        // remaining stays empty — next advance() will set done=true
        return;
    }
    auto pos = f.tellg();
    if (pos < 0) {
        impl_->current = tl::unexpected(parse_error::file_not_found);
        return;
    }
    auto size = static_cast<std::size_t>(pos);
    impl_->owned_buf.resize(size);
    f.seekg(0);
    if (!f.read(impl_->owned_buf.data(), static_cast<std::streamsize>(size))) {
        impl_->current = tl::unexpected(parse_error::file_not_found);
        return;
    }
    impl_->src       = impl_->owned_buf;
    impl_->remaining = impl_->owned_buf;
    impl_->advance();
}

pgn::game_stream::game_stream(std::string_view input)
    : impl_(std::make_unique<impl>())
{
    impl_->src       = input;
    impl_->remaining = input;
    impl_->advance();
}

pgn::game_stream::~game_stream() = default;

pgn::game_stream::game_stream(game_stream&&) noexcept = default;
pgn::game_stream& pgn::game_stream::operator=(game_stream&&) noexcept = default;

// ── Iterator ──────────────────────────────────────────────────────────────────

pgn::game_stream::iterator pgn::game_stream::begin()
{
    return iterator{impl_.get()};
}

pgn::game_stream::iterator::reference
pgn::game_stream::iterator::operator*() noexcept
{
    return impl_->current;
}

pgn::game_stream::iterator::pointer
pgn::game_stream::iterator::operator->() noexcept
{
    return &impl_->current;
}

pgn::game_stream::iterator& pgn::game_stream::iterator::operator++()
{
    impl_->advance();
    return *this;
}

bool pgn::game_stream::iterator::operator==(std::default_sentinel_t) const noexcept
{
    return impl_ == nullptr || impl_->done;
}

std::size_t pgn::game_stream::iterator::byte_offset() const noexcept
{
    return impl_ ? impl_->current_offset : 0;
}
