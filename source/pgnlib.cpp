#include <filesystem>
#include <fstream>
#include <istream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <lexy/action/parse.hpp>
#include <lexy/input/file.hpp>
#include <lexy/input/string_input.hpp>
#include <lexy_ext/report_error.hpp>

#include "grammar.hpp"
#include "pgnlib/import.hpp"
#include "pgnlib/pgnlib.hpp"

// ── Silent error sink ─────────────────────────────────────────────────────────
//
// lexy requires a non-void sink return type (validate.hpp static_assert), so
// lexy::noop cannot be used directly.  This sink counts errors silently.

namespace {
struct silent_errors {
    struct sink_t {
        std::size_t count = 0;
        using return_type = std::size_t;
        template <typename Input, typename Reader, typename Tag>
        void operator()(lexy::error_context<Input> const&,
                        lexy::error<Reader, Tag> const&) { ++count; }
        std::size_t finish() && { return count; }
    };
    constexpr auto sink() const { return sink_t{}; }
};
constexpr silent_errors no_stderr_errors;
} // namespace

// ── Internal helper ───────────────────────────────────────────────────────────

static auto parse_one_game(std::string_view slice)
    -> tl::expected<pgn::game, pgn::parse_error>
{
    auto in = lexy::string_input<lexy::utf8_encoding>(slice.data(), slice.size());
    auto result = lexy::parse<pgn::grammar::single_game>(in, no_stderr_errors);
    if (!result.has_value() || result.error_count() > 0)
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

    auto result = lexy::parse<pgn::grammar::pgn_file>(file.buffer(), no_stderr_errors);

    if (!result.has_value() || result.error_count() > 0)
        return tl::unexpected(parse_error::syntax_error);

    return std::move(result).value();
}

auto pgn::parse_file(std::filesystem::path const& path, std::string& diagnostics)
    -> tl::expected<std::vector<pgn::game>, pgn::parse_error>
{
    auto file = lexy::read_file<lexy::utf8_encoding>(path.c_str());
    if (!file)
        return tl::unexpected(parse_error::file_not_found);

    auto sink = lexy_ext::report_error.path(path.c_str())
                                      .to(std::back_inserter(diagnostics));
    auto result = lexy::parse<pgn::grammar::pgn_file>(file.buffer(), sink);

    if (!result.has_value() || result.error_count() > 0)
        return tl::unexpected(parse_error::syntax_error);

    return std::move(result).value();
}

auto pgn::parse_string(std::string_view input)
    -> tl::expected<std::vector<pgn::game>, pgn::parse_error>
{
    auto in = lexy::string_input<lexy::utf8_encoding>(input.data(), input.size());

    auto result = lexy::parse<pgn::grammar::pgn_file>(in, no_stderr_errors);

    if (!result.has_value() || result.error_count() > 0)
        return tl::unexpected(parse_error::syntax_error);

    return std::move(result).value();
}

auto pgn::parse_string(std::string_view input, std::string& diagnostics)
    -> tl::expected<std::vector<pgn::game>, pgn::parse_error>
{
    auto in = lexy::string_input<lexy::utf8_encoding>(input.data(), input.size());

    auto sink = lexy_ext::report_error.to(std::back_inserter(diagnostics));
    auto result = lexy::parse<pgn::grammar::pgn_file>(in, sink);

    if (!result.has_value() || result.error_count() > 0)
        return tl::unexpected(parse_error::syntax_error);

    return std::move(result).value();
}

// ── Game-boundary splitter (shared by game_stream and import_stream) ──────────
//
// Returns { game_slice, rest_starting_at_next_'[' }.
//
// Heuristic: a blank line followed by '[' is the standard PGN inter-game
// separator.  Handles LF (\n\n), CRLF (\r\n\r\n), and mixed line endings.

static auto pgn_split_game(std::string_view sv)
    -> std::pair<std::string_view, std::string_view>
{
    std::size_t pos = 0;
    bool in_brace = false;
    bool in_quote = false;

    while (pos < sv.size()) {
        char c = sv[pos];

        if (in_quote) {
            if (c == '\\') { pos += 2; continue; }
            if (c == '"')  { in_quote = false; }
            ++pos; continue;
        }
        if (c == '"') { in_quote = true; ++pos; continue; }

        if (in_brace) {
            if (c == '}') in_brace = false;
            ++pos; continue;
        }
        if (c == '{') { in_brace = true; ++pos; continue; }

        if (c == '%') {
            auto eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) return {sv, {}};
            pos = eol + 1; continue;
        }

        if (c == '\n') {
            std::size_t next = pos + 1;
            if (next < sv.size() && sv[next] == '\r') ++next;

            if (next < sv.size() && sv[next] == '\n') {
                std::size_t rest = next + 1;
                while (rest < sv.size() && (sv[rest] == '\r' || sv[rest] == '\n'))
                    ++rest;

                while (rest < sv.size()) {
                    while (rest < sv.size() && (sv[rest] == ' ' || sv[rest] == '\t'))
                        ++rest;
                    if (rest >= sv.size() || sv[rest] != '%') break;
                    auto eol = sv.find('\n', rest);
                    if (eol == std::string_view::npos) { rest = sv.size(); break; }
                    rest = eol + 1;
                    while (rest < sv.size() && (sv[rest] == '\r' || sv[rest] == '\n'))
                        ++rest;
                }

                if (rest < sv.size() && sv[rest] == '[')
                    return {sv.substr(0, next + 1), sv.substr(rest)};
            }
        }

        ++pos;
    }
    return {sv, {}};
}

// ── game_stream ───────────────────────────────────────────────────────────────

struct pgn::game_stream::impl {
    std::string      owned_buf;
    std::string_view src;
    std::string_view remaining;
    std::size_t      current_offset{0};
    tl::expected<game, parse_error> current;
    bool done = false;

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

        auto [slice, rest] = pgn_split_game(remaining);
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

// ── Import scanner (hand-rolled, zero-allocation for string data) ─────────────
//
// Parses a single game slice into a pgn::import_game.  All string_view fields
// point into the slice (which itself points into the owning buffer).
//
// Only the main line is parsed.  Variations ( ... ), comments { ... }, and
// NAGs $N are consumed and discarded.
//
// Tag values are returned as raw bytes between the outer quotes; backslash
// escape sequences (e.g. \") are NOT unescaped.

namespace {

using Cptr = const char*;

static void iscan_skip_ws(Cptr& p, Cptr end)
{
    for (;;) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
            ++p;
        if (p < end && *p == '%') {
            while (p < end && *p != '\n') ++p;
            continue;
        }
        break;
    }
}

static void iscan_skip_comment(Cptr& p, Cptr end)
{
    ++p;  // consume '{'
    while (p < end && *p != '}') ++p;
    if (p < end) ++p;  // consume '}'
}

// Skip ( variation ) with arbitrary nesting.  Also handles { } and " " inside.
static void iscan_skip_variation(Cptr& p, Cptr end)
{
    int depth = 0;
    while (p < end) {
        char c = *p++;
        if      (c == '(') { ++depth; }
        else if (c == ')') { if (--depth == 0) return; }
        else if (c == '{') { while (p < end && *p != '}') ++p; if (p < end) ++p; }
        else if (c == '"') {
            while (p < end) {
                if (*p == '\\') { if (p + 1 < end) { p += 2; } else { p = end; } continue; }
                if (*p++ == '"') break;
            }
        }
    }
}

static void iscan_skip_nag(Cptr& p, Cptr end)
{
    ++p;  // consume '$'
    while (p < end && *p >= '0' && *p <= '9') ++p;
}

static void iscan_skip_annotations(Cptr& p, Cptr end)
{
    for (;;) {
        iscan_skip_ws(p, end);
        if (p >= end) break;
        if (*p == '{') { iscan_skip_comment(p, end); continue; }
        if (*p == '$') { iscan_skip_nag(p, end);     continue; }
        if (*p == '(') { iscan_skip_variation(p, end); continue; }
        break;
    }
}

// Parse [Key "RawValue"].  Precondition: *p == '['.
// Returns false on malformed input (p left in indeterminate state on failure).
static bool iscan_parse_tag(Cptr& p, Cptr end,
                             std::string_view& key, std::string_view& value)
{
    ++p;  // consume '['
    iscan_skip_ws(p, end);

    // Key: [A-Za-z_][A-Za-z0-9_]*
    auto is_key_head = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    auto is_key_tail = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '_';
    };
    if (p >= end || !is_key_head(*p)) return false;
    Cptr ks = p;
    while (p < end && is_key_tail(*p)) ++p;
    key = {ks, static_cast<std::size_t>(p - ks)};

    iscan_skip_ws(p, end);
    if (p >= end || *p != '"') return false;
    ++p;  // consume opening '"'

    Cptr vs = p;
    while (p < end) {
        if (*p == '\\') { if (p + 1 >= end) return false; p += 2; continue; }
        if (*p == '"') break;
        ++p;
    }
    if (p >= end) return false;
    value = {vs, static_cast<std::size_t>(p - vs)};
    ++p;  // consume closing '"'

    iscan_skip_ws(p, end);
    if (p >= end || *p != ']') return false;
    ++p;  // consume ']'
    return true;
}

// Returns the integer part of a move-number token (digits + dots), or 0.
// Restores p if the digits are not followed by '.'.
static int iscan_maybe_movenum(Cptr& p, Cptr end)
{
    if (p >= end || *p < '0' || *p > '9') return 0;
    Cptr save = p;
    int n = 0;
    while (p < end && *p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); ++p; }
    if (p < end && *p == '.') {
        while (p < end && *p == '.') ++p;
        return n;
    }
    p = save;
    return 0;
}

// Parse a SAN token (null move "--" or alpha-leading identifier).
static std::string_view iscan_parse_san(Cptr& p, Cptr end)
{
    // Null move "--"
    if (static_cast<std::size_t>(end - p) >= 2 && p[0] == '-' && p[1] == '-') {
        std::string_view sv{p, 2};
        p += 2;
        return sv;
    }
    auto is_alpha = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    };
    auto is_san_cont = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') ||
               c == '-' || c == '+' || c == '#' || c == '=' || c == '!' || c == '?';
    };
    if (p >= end || !is_alpha(*p)) return {};
    Cptr s = p++;
    while (p < end && is_san_cont(*p)) ++p;
    return {s, static_cast<std::size_t>(p - s)};
}

// Try to match a result token.  Does NOT skip leading whitespace.
// Returns nullopt (and leaves p unchanged) if no match.
static std::optional<pgn::result> iscan_try_result(Cptr& p, Cptr end)
{
    auto rem = static_cast<std::size_t>(end - p);
    if (rem == 0) return std::nullopt;
    if (*p == '*') { ++p; return pgn::result::unknown; }
    if (rem >= 3 && p[0] == '1' && p[1] == '-' && p[2] == '0') { p += 3; return pgn::result::white; }
    if (rem >= 3 && p[0] == '0' && p[1] == '-' && p[2] == '1') { p += 3; return pgn::result::black; }
    if (rem >= 7 && p[0] == '1' && p[1] == '/' &&
        std::string_view{p, 7} == "1/2-1/2") { p += 7; return pgn::result::draw; }
    return std::nullopt;
}

static auto iscan_parse_one(std::string_view slice)
    -> tl::expected<pgn::import_game, pgn::parse_error>
{
    Cptr p   = slice.data();
    Cptr end = p + slice.size();

    pgn::import_game game;

    // ── Tag section ──────────────────────────────────────────────────────
    iscan_skip_ws(p, end);
    while (p < end && *p == '[') {
        std::string_view key, value;
        if (!iscan_parse_tag(p, end, key, value))
            return tl::unexpected(pgn::parse_error::syntax_error);
        game.tags.push_back({key, value});
        iscan_skip_ws(p, end);
    }
    if (game.tags.empty())
        return tl::unexpected(pgn::parse_error::syntax_error);

    // ── Move section ─────────────────────────────────────────────────────
    for (;;) {
        iscan_skip_ws(p, end);
        if (p >= end)
            return tl::unexpected(pgn::parse_error::syntax_error);

        if (auto r = iscan_try_result(p, end)) {
            game.result = *r;
            return game;
        }

        int num = iscan_maybe_movenum(p, end);
        iscan_skip_ws(p, end);

        auto san = iscan_parse_san(p, end);
        if (san.empty())
            return tl::unexpected(pgn::parse_error::syntax_error);

        game.moves.push_back({num, san});
        iscan_skip_annotations(p, end);
    }
}

} // anonymous namespace

// ── import_stream ─────────────────────────────────────────────────────────────

struct pgn::import_stream::impl {
    std::string      owned_buf;
    std::string_view src;
    std::string_view remaining;
    std::istream*    input = nullptr;
    std::size_t      buffer_offset = 0;
    std::size_t      consumed_prefix = 0;
    std::size_t      buffer_size = 0;
    std::size_t      current_offset{0};
    tl::expected<import_game, parse_error> current;
    bool done = false;

    void advance() {
        if (input != nullptr) {
            advance_input();
            return;
        }
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

        auto [slice, rest] = pgn_split_game(remaining);
        remaining = rest;
        current   = iscan_parse_one(slice);
    }

    // Incremental path for the std::istream overload: owned_buf holds only the
    // still-unconsumed tail (at most one refill's worth past the last complete
    // game boundary found), so peak memory is bounded by buffer_size plus the
    // largest single game's raw text rather than by the total input size.
    // consumed_prefix defers the erase of the previous game's bytes to the
    // start of the *next* advance_input() call, so the string_views in
    // `current` (which alias into owned_buf) stay valid until the caller
    // advances the iterator again.
    void advance_input() {
        if (consumed_prefix > 0U) {
            owned_buf.erase(0, consumed_prefix);
            buffer_offset += consumed_prefix;
            consumed_prefix = 0;
        }
        current_offset = buffer_offset;

        for (;;) {
            auto pos = owned_buf.find_first_not_of(" \t\r\n");
            while (pos != std::string::npos && owned_buf[pos] == '%') {
                auto const eol = owned_buf.find('\n', pos);
                if (eol == std::string::npos) {
                    break;
                }
                pos = owned_buf.find_first_not_of(" \t\r\n", eol + 1U);
            }
            if (pos != std::string::npos && pos > 0U) {
                buffer_offset += pos;
                owned_buf.erase(0, pos);
                current_offset = buffer_offset;
            }

            auto const [slice, rest] = pgn_split_game(owned_buf);
            if (!rest.empty()) {
                auto const rest_offset = static_cast<std::size_t>(rest.data() - owned_buf.data());
                consumed_prefix = rest_offset;
                current         = iscan_parse_one(slice);
                return;
            }

            auto chunk = std::string(buffer_size, '\0');
            input->read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            auto const read_count = input->gcount();
            if (read_count > 0) {
                owned_buf.append(chunk.data(), static_cast<std::size_t>(read_count));
                continue;
            }
            if (input->bad()) {
                // Sever the stream pointer so the *next* advance() call falls
                // through to the in-memory branch above with empty
                // remaining/src, which sets done without touching current
                // again -- the same one-error-then-stop pattern the
                // path-constructor's open failure already relies on (see
                // import_stream(path) below and the "file_not_found" test).
                current = tl::unexpected(parse_error::file_not_found);
                input   = nullptr;
                return;
            }
            // EOF: no more '[Event' boundary will ever follow, so the whole
            // remaining buffer is the last game.
            if (owned_buf.empty()) {
                done = true;
                return;
            }
            current         = iscan_parse_one(owned_buf);
            consumed_prefix = owned_buf.size();
            return;
        }
    }
};

pgn::import_stream::import_stream(std::filesystem::path const& path)
    : impl_(std::make_unique<impl>())
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        impl_->current = tl::unexpected(parse_error::file_not_found);
        return;
    }
    auto pos = f.tellg();
    if (pos < 0) { impl_->current = tl::unexpected(parse_error::file_not_found); return; }
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

pgn::import_stream::import_stream(std::string_view input)
    : impl_(std::make_unique<impl>())
{
    impl_->src       = input;
    impl_->remaining = input;
    impl_->advance();
}

pgn::import_stream::import_stream(std::istream& input, std::size_t buffer_size)
    : impl_(std::make_unique<impl>())
{
    if (buffer_size == 0U) {
        impl_->current = tl::unexpected(parse_error::syntax_error);
        return;
    }
    impl_->input       = &input;
    impl_->buffer_size = buffer_size;
    impl_->advance();
}

pgn::import_stream::~import_stream()                                    = default;
pgn::import_stream::import_stream(import_stream&&) noexcept             = default;
pgn::import_stream& pgn::import_stream::operator=(import_stream&&) noexcept = default;

pgn::import_stream::iterator pgn::import_stream::begin()
{
    return iterator{impl_.get()};
}

pgn::import_stream::iterator::reference
pgn::import_stream::iterator::operator*() noexcept
{
    return impl_->current;
}

pgn::import_stream::iterator::pointer
pgn::import_stream::iterator::operator->() noexcept
{
    return &impl_->current;
}

pgn::import_stream::iterator& pgn::import_stream::iterator::operator++()
{
    impl_->advance();
    return *this;
}

bool pgn::import_stream::iterator::operator==(std::default_sentinel_t) const noexcept
{
    return impl_ == nullptr || impl_->done;
}

std::size_t pgn::import_stream::iterator::byte_offset() const noexcept
{
    return impl_ ? impl_->current_offset : 0;
}
