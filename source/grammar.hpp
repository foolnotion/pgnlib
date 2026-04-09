#pragma once

// Internal grammar — not installed, not part of the public API.
// Requires: lexy (foonathan/lexy)

#include <optional>
#include <string>
#include <vector>

#include <lexy/callback.hpp>
#include <lexy/dsl.hpp>

#include "pgnlib/types.hpp"

namespace pgn::grammar {

namespace dsl = lexy::dsl;

// ─── Whitespace ───────────────────────────────────────────────────────────────
//
// Skipped automatically between tokens in every non-token production:
//   • ASCII space / tab / newline
//   • % line comments  (common in Lichess exports: %clk 0:01:23)

static constexpr auto whitespace
    = dsl::ascii::space
    | dsl::lit_c<'%'> >> dsl::until(dsl::newline);

// ─── Result token (shared between lookahead and parsing) ──────────────────────

static constexpr auto result_token
    = dsl::literal_set(LEXY_LIT("1/2-1/2"), LEXY_LIT("1-0"), LEXY_LIT("0-1"), LEXY_LIT("*"));

// ─── Forward declaration (variation_rule ↔ move_sequence recursion) ───────────

struct move_sequence;

// ─── Primitives ───────────────────────────────────────────────────────────────

// $N  (N = 0..255) — lexy rejects values that overflow uint8_t.
struct nag_rule : lexy::token_production {
    static constexpr auto rule
        = dsl::lit_c<'$'> >> dsl::integer<std::uint8_t>(dsl::digits<>);
    static constexpr auto value
        = lexy::callback<pgn::nag>([](std::uint8_t n) { return pgn::nag{n}; });
};

// { comment text }  — nested braces not supported (rare in practice)
// dsl::delimited handles opening/closing delimiters and applies no_whitespace
// internally, so the comment text is captured verbatim.
struct comment_rule {
    static constexpr auto rule
        = dsl::delimited(dsl::lit_c<'{'>, dsl::lit_c<'}'>)(dsl::code_point);
    static constexpr auto value = lexy::as_string<std::string, lexy::utf8_encoding>;
};

// Tag key: alpha/underscore leading, alphanumeric/underscore trailing
struct tag_key {
    static constexpr auto rule
        = dsl::identifier(dsl::ascii::alpha_underscore,
                          dsl::ascii::alpha_digit_underscore);
    static constexpr auto value = lexy::as_string<std::string>;
};

// "quoted string value" — backslash-escaped quotes and backslashes allowed.
// Uses code_point (not ascii::print) so multi-byte UTF-8 names work.
struct tag_value_str : lexy::token_production {
    static constexpr auto rule = dsl::quoted(
        dsl::code_point,
        dsl::backslash_escape
            .capture(dsl::lit_c<'"'>)
            .capture(dsl::lit_c<'\\'>));
    static constexpr auto value = lexy::as_string<std::string, lexy::utf8_encoding>;
};

// [Key "Value"]
struct tag_pair {
    static constexpr auto whitespace = pgn::grammar::whitespace;
    static constexpr auto rule
        = dsl::lit_c<'['>
        >> dsl::p<tag_key>
        + dsl::p<tag_value_str>
        + dsl::lit_c<']'>;
    static constexpr auto value = lexy::callback<pgn::tag>(
        [](std::string key, std::string val) {
            return pgn::tag{std::move(key), std::move(val)};
        });
};

// One or more tag pairs (PGN requires at least the 7 mandatory tags)
struct tag_list {
    static constexpr auto whitespace = pgn::grammar::whitespace;
    static constexpr auto rule
        = dsl::list(dsl::peek(dsl::lit_c<'['>) >> dsl::p<tag_pair>);
    static constexpr auto value = lexy::as_list<std::vector<pgn::tag>>;
};

// ─── SAN move token ───────────────────────────────────────────────────────────
//
// Handles:
//   regular moves  — e4, Nf3, O-O, O-O-O, e8=Q, Rxd8#, Nxf3+
//   annotation     — !, ?, !!, ??, !?, ?!  (sometimes embedded in SAN)
//   null move      — --

struct san_move {
    // null move first so "--" isn't matched as a partial identifier
    static constexpr auto rule
        = dsl::capture(LEXY_LIT("--"))
        | dsl::identifier(
            dsl::ascii::alpha,
            dsl::ascii::alpha_digit
                / dsl::lit_c<'-'>
                / dsl::lit_c<'+'>
                / dsl::lit_c<'#'>
                / dsl::lit_c<'='>
                / dsl::lit_c<'!'>
                / dsl::lit_c<'?'>);
    static constexpr auto value = lexy::as_string<std::string>;
};

// ─── Move number ──────────────────────────────────────────────────────────────
//
// Matches "1.", "10.", "1..." (black continuation after variation), etc.
// Returns the integer part; dots are consumed and discarded.

struct move_number : lexy::token_production {
    static constexpr auto rule
        = dsl::integer<int>(dsl::digits<>)
        + dsl::lit_c<'.'>
        + dsl::while_(dsl::lit_c<'.'>);
    static constexpr auto value = lexy::callback<int>(
        [](int n) { return n; });
};

// ─── Result ───────────────────────────────────────────────────────────────────

// 1/2-1/2 listed first so the prefix "1" doesn't match "1-0" first.
static constexpr auto result_table
    = lexy::symbol_table<pgn::result>
          .map<LEXY_SYMBOL("1/2-1/2")>(pgn::result::draw)
          .map<LEXY_SYMBOL("1-0")>(pgn::result::white)
          .map<LEXY_SYMBOL("0-1")>(pgn::result::black)
          .map<LEXY_SYMBOL("*")>(pgn::result::unknown);

struct result_rule {
    static constexpr auto rule = dsl::symbol<result_table>(result_token);
    static constexpr auto value = lexy::forward<pgn::result>;
};

// ─── Optional wrappers (collapse lexy::nullopt into sensible defaults) ────────

struct maybe_move_number {
    static constexpr auto rule
        = dsl::opt(dsl::peek(dsl::digits<>) >> dsl::p<move_number>);
    static constexpr auto value = lexy::callback<int>(
        [](lexy::nullopt) { return 0; },
        [](int n) { return n; });
};

struct maybe_comment {
    static constexpr auto rule
        = dsl::opt(dsl::peek(dsl::lit_c<'{'>) >> dsl::p<comment_rule>);
    static constexpr auto value = lexy::callback<std::optional<std::string>>(
        [](lexy::nullopt) -> std::optional<std::string> { return std::nullopt; },
        [](std::string s) -> std::optional<std::string> { return s; });
};

// ─── NAG list  (zero or more) ─────────────────────────────────────────────────

struct nag_list {
    static constexpr auto whitespace = pgn::grammar::whitespace;
    static constexpr auto rule
        = dsl::opt(dsl::list(dsl::peek(dsl::lit_c<'$'>) >> dsl::p<nag_rule>));
    static constexpr auto value = lexy::as_list<std::vector<pgn::nag>>;
};

// ─── Variation (recursive through move_sequence) ──────────────────────────────

struct variation_rule {
    static constexpr auto whitespace = pgn::grammar::whitespace;
    static constexpr auto rule
        = dsl::lit_c<'('>
        >> dsl::recurse<move_sequence>
        + dsl::lit_c<')'>;
    static constexpr auto value = lexy::callback<pgn::variation>(
        [](std::vector<pgn::move_node> moves) {
            return pgn::variation{std::move(moves)};
        });
};

// Zero or more variations
struct variation_list {
    static constexpr auto whitespace = pgn::grammar::whitespace;
    static constexpr auto rule
        = dsl::opt(dsl::list(dsl::peek(dsl::lit_c<'('>) >> dsl::p<variation_rule>));
    static constexpr auto value = lexy::as_list<std::vector<pgn::variation>>;
};

// ─── Single move node ─────────────────────────────────────────────────────────
//
// PGN allows comments and NAGs in any order after the SAN.  We accommodate the
// two most common orderings by allowing one comment slot before and one after
// the NAG list.  Both produce at most one stored comment (concatenated when
// both slots are filled, which is rare in practice).

struct move_node_rule {
    static constexpr auto whitespace = pgn::grammar::whitespace;
    static constexpr auto rule
        = dsl::p<maybe_move_number>
        + dsl::p<san_move>
        + dsl::p<maybe_comment>   // comment before NAGs
        + dsl::p<nag_list>
        + dsl::p<maybe_comment>   // comment after NAGs (e.g. "Bxd1 $19 {text}")
        + dsl::p<variation_list>;
    static constexpr auto value = lexy::callback<pgn::move_node>(
        [](int num,
           std::string san,
           std::optional<std::string> pre,
           std::vector<pgn::nag> nags,
           std::optional<std::string> post,
           std::vector<pgn::variation> vars) {
            std::optional<std::string> comment;
            if (pre && post)
                comment = *pre + " " + *post;
            else if (pre)
                comment = std::move(pre);
            else
                comment = std::move(post);
            return pgn::move_node{
                num,
                std::move(san),
                std::move(comment),
                std::move(nags),
                std::move(vars),
            };
        });
};

// ─── Move sequence ────────────────────────────────────────────────────────────
//
// Zero or more move nodes, stopping at a result token or closing paren.

static constexpr auto seq_end = result_token / dsl::lit_c<')'>;

struct move_sequence {
    static constexpr auto whitespace = pgn::grammar::whitespace;
    static constexpr auto rule
        = dsl::opt(dsl::list(dsl::peek_not(seq_end) >> dsl::p<move_node_rule>));
    static constexpr auto value = lexy::as_list<std::vector<pgn::move_node>>;
};

// ─── Full game ────────────────────────────────────────────────────────────────

struct game_rule {
    static constexpr auto whitespace = pgn::grammar::whitespace;
    static constexpr auto rule
        = dsl::p<tag_list>
        + dsl::p<move_sequence>
        + dsl::p<result_rule>;
    static constexpr auto value = lexy::callback<pgn::game>(
        [](std::vector<pgn::tag> tags,
           std::vector<pgn::move_node> moves,
           pgn::result res) {
            return pgn::game{std::move(tags), std::move(moves), res};
        });
};

// ─── PGN file (sequence of games) ─────────────────────────────────────────────

struct pgn_file {
    static constexpr auto whitespace = pgn::grammar::whitespace;
    static constexpr auto rule
        = dsl::opt(dsl::list(dsl::peek(dsl::lit_c<'['>) >> dsl::p<game_rule>));
    static constexpr auto value = lexy::as_list<std::vector<pgn::game>>;
};

} // namespace pgn::grammar
