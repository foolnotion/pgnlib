## PGNLib

Parser library for chess PGN format, based on the Lexy library.

A PGN file is a sequence of games, each consisting of:
```
[Tag "Value"]          <- tag pairs (7 mandatory, N optional)
[Tag "Value"]

1. e4 e5               <- movetext
2. Nf3 { comment } Nc6
3. Bb5 $1 (3. d4)      <- NAG + variation
1-0                    <- result
```
The grammar is recursive because variations can contain variations.

### Data model

```cpp
// types.hpp (pgn module)
namespace pgn {

struct tag {
    std::string key;
    std::string value;
};

enum class result { white, black, draw, unknown };

struct nag {
    int value; // 0–255
};

struct move_node; // forward declaration for recursion

struct variation {
    std::vector<move_node> moves;
};

struct move_node {
    int                          number;       // 0 if continuation
    std::string                  san;          // "e4", "Nf3", "O-O"
    std::optional<std::string>   comment;
    std::vector<nag>             nags;
    std::vector<variation>       variations;   // recursive
};

struct game {
    std::vector<tag>       tags;
    std::vector<move_node> moves;
    result                 result;
};

} // namespace pgn
```

### Grammar sketch

```cpp
#include <lexy/dsl.hpp>
#include <lexy/action/parse.hpp>
#include <lexy/callback.hpp>
#include <lexy/input/string_input.hpp>

namespace pgn_grammar {
namespace dsl = lexy::dsl;

// ─── Primitives ───────────────────────────────────────────────

// Whitespace: spaces, tabs, newlines — skipped everywhere
struct ws {
    static constexpr auto rule  = dsl::whitespace(dsl::ascii::space);
};

// Integer: used for move numbers and NAGs
struct integer {
    static constexpr auto rule  = dsl::integer<int>(dsl::digits<>);
    static constexpr auto value = lexy::as_integer<int>;
};

// NAG: $0–$255
struct nag {
    static constexpr auto rule =
        dsl::lit_c<'$'> >> dsl::p<integer>;
    static constexpr auto value =
        lexy::callback<pgn::nag>([](int n) { return pgn::nag{n}; });
};

// Comment: { any text }
struct comment {
    static constexpr auto rule =
        dsl::lit_c<'{'> >>
        dsl::capture(dsl::until(dsl::lit_c<'}'>));
    static constexpr auto value =
        lexy::as_string<std::string>;
};

// Tag value: "quoted string" — handles escaped quotes
struct tag_value {
    static constexpr auto rule =
        dsl::quoted(
            dsl::ascii::print,
            dsl::backslash_escape.capture(dsl::lit_c<'"'>)
        );
    static constexpr auto value = lexy::as_string<std::string>;
};

// Tag pair: [Name "Value"]
struct tag_pair {
    static constexpr auto rule =
        dsl::lit_c<'['> >>
        dsl::capture(dsl::identifier(dsl::ascii::alpha_digit_underscore)) +
        dsl::p<tag_value> +
        dsl::lit_c<']'>;
    static constexpr auto value =
        lexy::callback<pgn::tag>([](auto key, auto val) {
            return pgn::tag{ std::string(key.begin(), key.end()), val };
        });
};

// ─── SAN Move Token ───────────────────────────────────────────

// Matches: e4, Nf3, O-O, O-O-O, e8=Q, Nxf3+, Rxd8#
// Note: !, ?, !!, ??, !?, ?! are annotation glyphs sometimes
// embedded in SAN in real-world PGN — handle them here
struct san_move {
    static constexpr auto rule =
        dsl::capture(
            dsl::identifier(
                // first char: piece letter or pawn file
                dsl::ascii::alpha,
                // subsequent: alphanumeric + special chars
                dsl::ascii::alpha_digit /
                dsl::lit_c<'-'> / dsl::lit_c<'+'> /
                dsl::lit_c<'#'> / dsl::lit_c<'='> /
                dsl::lit_c<'!'> / dsl::lit_c<'?'>
            )
        );
    static constexpr auto value = lexy::as_string<std::string>;
};

// ─── Game Result ──────────────────────────────────────────────

struct result {
    static constexpr auto rule =
        dsl::lit<"1-0">     |
        dsl::lit<"0-1">     |
        dsl::lit<"1/2-1/2"> |
        dsl::lit_c<'*'>;
    static constexpr auto value =
        lexy::callback<pgn::result>(
            [](auto lexeme) -> pgn::result {
                auto s = std::string_view(lexeme.begin(), lexeme.end());
                if (s == "1-0")     return pgn::result::white;
                if (s == "0-1")     return pgn::result::black;
                if (s == "1/2-1/2") return pgn::result::draw;
                return pgn::result::unknown;
            });
};

// ─── Move Number ──────────────────────────────────────────────

// Matches: "1." or "1..." (black continuation)
struct move_number {
    static constexpr auto rule =
        dsl::p<integer> >>
        dsl::lit_c<'.'> >>
        dsl::opt(dsl::lit<"..">);  // optional ".." for "..."
};

// ─── Variation (recursive) ────────────────────────────────────

// Forward-declare move_sequence for mutual recursion
struct move_sequence;

struct variation {
    static constexpr auto rule =
        dsl::lit_c<'('> >>
        dsl::recurse<move_sequence> >>
        dsl::lit_c<')'>;
    static constexpr auto value =
        lexy::callback<pgn::variation>([](auto moves) {
            return pgn::variation{ std::move(moves) };
        });
};

// ─── Single Move Node ─────────────────────────────────────────

struct move_node {
    static constexpr auto rule =
        dsl::opt(dsl::p<move_number>) >>
        dsl::p<san_move> >>
        dsl::opt(dsl::p<comment>) >>
        dsl::opt(dsl::list(dsl::p<nag>)) >>
        dsl::opt(dsl::list(dsl::p<variation>));
};

// ─── Move Sequence ────────────────────────────────────────────

struct move_sequence {
    static constexpr auto rule =
        dsl::list(dsl::p<move_node>,
                  dsl::trailing_sep(dsl::p<result>));
};

// ─── Full Game ────────────────────────────────────────────────

struct game {
    static constexpr auto rule =
        dsl::list(dsl::p<tag_pair>) >>
        dsl::p<move_sequence>       >>
        dsl::p<result>;
    static constexpr auto value =
        lexy::callback<pgn::game>([](auto tags, auto moves, auto res) {
            return pgn::game{ std::move(tags), std::move(moves), res };
        });
};

// ─── PGN File (sequence of games) ────────────────────────────

struct pgn_file {
    static constexpr auto rule  = dsl::list(dsl::p<game>);
    static constexpr auto value = lexy::as_list<std::vector<pgn::game>>;
};

} // namespace pgn_grammar
```

### Parser entry point

```cpp
#include <lexy/action/parse.hpp>
#include <lexy/input/file.hpp>
#include <lexy_ext/report_error.hpp>

auto parse_pgn_file(std::filesystem::path const& path)
    -> std::optional<std::vector<pgn::game>>
{
    auto file = lexy::read_file<lexy::utf8_encoding>(path.c_str());
    if (!file) {
        return std::nullopt; // file not found
    }

    auto result = lexy::parse<pgn_grammar::pgn_file>(
        file.buffer(),
        lexy_ext::report_error.path(path.c_str())
    );

    if (!result.has_value()) {
        return std::nullopt;
    }
    return std::move(result).value();
}
```

### Edge cases

| Edge Case |	Handling Strategy |
|-----------|---------------------|
| !, ?, !!, ??, !?, ?! in SAN | Include in san_move identifier chars or strip as post-process |
| Null move | Add as alternative in san_move |
| Missing result token | Make result optional at game end |
| UTF-8 / Latin-1 player names | Use lexy::utf8_encoding or lexy::byte_encoding |
| % clock annotations (%clk 1:30:00) | Treat as line comment — skip to end of line |
| Nested {} in comments | dsl::until doesn't nest — use a counter-based custom rule |
| Multiple blank lines between games | Handled by whitespace skipping |
| Duplicate/missing mandatory tags | Validate after parsing, not during |


The %clk case is particularly common in Lichess exports and needs explicit handling:
```cpp
// Line comment: % ... \n (used for clock annotations in Lichess PGN)
struct line_comment {
    static constexpr auto rule =
        dsl::lit_c<'%'> >>
        dsl::until(dsl::newline);
};
```

## Acceptance criteria

PGN parser built on foonathan::lexy. Produces pgn::game structs with SAN
strings — no chess logic dependency.

| Task                                         | Needed By        |
|----------------------------------------------|------------------|
| lexy grammar: tags, SAN tokens, NAGs         | 002-import       |
| Recursive variation support (depth >= 10)    | 002-import       |
| Comment handling ({})                        | 002-import       |
| Result tokens (1-0, 0-1, 1/2-1/2, *)        | 002-import       |
| Null move support (--)                       | 002-import       |
| %clk line comment handling                   | 002-import       |
| UTF-8 player names                           | 002-import       |
| Malformed game recovery (skip + log)         | 002-import       |
| parse_file() and parse_string() public API   | 002-import       |
| 100K games in under 10 s                     | 002-import       |

Minimum version: all tasks above complete, tagged, and pinned in flake.lock.

Does not depend on chesslib. SAN validation happens in motif-chess's import
pipeline, not in pgnlib.