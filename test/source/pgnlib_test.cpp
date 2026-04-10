#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "pgnlib/pgnlib.hpp"

// Resolve the test data directory supplied by CMake.
static const std::filesystem::path test_data_dir{PGNLIB_TEST_DATA_DIR};

// Minimal valid game used as a baseline across multiple tests.
static constexpr std::string_view minimal_pgn = R"(
[Event "Test"]
[Site "?"]
[Date "2024.01.01"]
[Round "1"]
[White "Smith, John"]
[Black "Jones, Bob"]
[Result "1-0"]

1. e4 e5 2. Nf3 Nc6 3. Bb5 1-0
)";

TEST_CASE("parse minimal game", "[parser]")
{
    auto result = pgn::parse_string(minimal_pgn);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);

    auto const& game = result->front();
    CHECK(game.tags.size() == 7);
    CHECK(game.tags[0].key == "Event");
    CHECK(game.tags[0].value == "Test");
    CHECK(game.moves.size() == 5);
    CHECK(game.result == pgn::result::white);
}

TEST_CASE("move numbers are parsed", "[parser]")
{
    auto result = pgn::parse_string(minimal_pgn);
    REQUIRE(result.has_value());

    auto const& moves = result->front().moves;
    CHECK(moves[0].number == 1);
    CHECK(moves[0].san == "e4");
    CHECK(moves[1].number == 0);  // black reply — no move number token
    CHECK(moves[1].san == "e5");
    CHECK(moves[2].number == 2);
    CHECK(moves[2].san == "Nf3");
}

TEST_CASE("result tokens", "[parser]")
{
    auto check = [](std::string_view suffix, pgn::result expected) {
        auto pgn = std::string(R"([Event "?"][Site "?"][Date "?"][Round "?"])"
                               R"([White "?"][Black "?"][Result ")");
        pgn += suffix;
        pgn += "\"]\n\n";
        pgn += suffix;
        pgn += "\n";
        return pgn::parse_string(pgn).transform([expected](auto games) {
            return games.front().result == expected;
        });
    };

    CHECK(check("1-0",     pgn::result::white).value_or(false));
    CHECK(check("0-1",     pgn::result::black).value_or(false));
    CHECK(check("1/2-1/2", pgn::result::draw).value_or(false));
    CHECK(check("*",       pgn::result::unknown).value_or(false));
}

TEST_CASE("comment is captured", "[parser]")
{
    constexpr std::string_view pgn = R"(
[Event "?"][Site "?"][Date "?"][Round "?"]
[White "?"][Black "?"][Result "1-0"]

1. e4 { White opens with pawn to e4. } e5 1-0
)";

    auto result = pgn::parse_string(pgn);
    REQUIRE(result.has_value());

    auto const& moves = result->front().moves;
    REQUIRE(moves.size() == 2);
    REQUIRE(moves[0].comment.has_value());
    CHECK(moves[0].comment.value().find("White opens") != std::string::npos);
    CHECK_FALSE(moves[1].comment.has_value());
}

TEST_CASE("NAGs are parsed", "[parser]")
{
    constexpr std::string_view pgn = R"(
[Event "?"][Site "?"][Date "?"][Round "?"]
[White "?"][Black "?"][Result "1-0"]

1. e4 $1 e5 $2 1-0
)";

    auto result = pgn::parse_string(pgn);
    REQUIRE(result.has_value());

    auto const& moves = result->front().moves;
    REQUIRE(moves[0].nags.size() == 1);
    CHECK(moves[0].nags[0].value == 1);
    REQUIRE(moves[1].nags.size() == 1);
    CHECK(moves[1].nags[0].value == 2);
}

TEST_CASE("variation is parsed", "[parser]")
{
    constexpr std::string_view pgn = R"(
[Event "?"][Site "?"][Date "?"][Round "?"]
[White "?"][Black "?"][Result "1-0"]

1. e4 e5 2. Nf3 (2. d4 exd4) Nc6 1-0
)";

    auto result = pgn::parse_string(pgn);
    REQUIRE(result.has_value());

    auto const& moves = result->front().moves;
    // Move 2 (Nf3) has one variation
    auto const& nf3 = moves[2];
    CHECK(nf3.san == "Nf3");
    REQUIRE(nf3.variations.size() == 1);
    auto const& var = nf3.variations[0];
    REQUIRE(var.moves.size() == 2);
    CHECK(var.moves[0].san == "d4");
    CHECK(var.moves[1].san == "exd4");
}

TEST_CASE("deeply nested variations (depth >= 10)", "[parser]")
{
    // Build a string with 10 levels of nesting.
    std::string pgn;
    pgn += R"([Event "?"][Site "?"][Date "?"][Round "?"])";
    pgn += R"([White "?"][Black "?"][Result "*"])";
    pgn += "\n\n1. e4 ";
    for (int i = 0; i < 10; ++i) pgn += "(1. d4 ";
    pgn += "e5 ";
    for (int i = 0; i < 10; ++i) pgn += ")";
    pgn += " *\n";

    auto result = pgn::parse_string(pgn);
    CHECK(result.has_value());
}

TEST_CASE("null move --", "[parser]")
{
    constexpr std::string_view pgn = R"(
[Event "?"][Site "?"][Date "?"][Round "?"]
[White "?"][Black "?"][Result "*"]

1. -- e5 *
)";

    auto result = pgn::parse_string(pgn);
    REQUIRE(result.has_value());
    CHECK(result->front().moves[0].san == "--");
}

TEST_CASE("percent-sign line comments are skipped", "[parser]")
{
    constexpr std::string_view pgn = R"(
[Event "?"][Site "?"][Date "?"][Round "?"]
[White "?"][Black "?"][Result "1-0"]

1. e4 e5
%clk 0:01:30
2. Nf3 Nc6 1-0
)";

    auto result = pgn::parse_string(pgn);
    REQUIRE(result.has_value());
    CHECK(result->front().moves.size() == 4);
}

TEST_CASE("multiple games in one string", "[parser]")
{
    constexpr std::string_view pgn = R"(
[Event "Game 1"][Site "?"][Date "?"][Round "1"]
[White "A"][Black "B"][Result "1-0"]

1. e4 1-0

[Event "Game 2"][Site "?"][Date "?"][Round "2"]
[White "C"][Black "D"][Result "0-1"]

1. d4 0-1
)";

    auto result = pgn::parse_string(pgn);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    CHECK((*result)[0].result == pgn::result::white);
    CHECK((*result)[1].result == pgn::result::black);
    CHECK((*result)[0].tags[0].value == "Game 1");
    CHECK((*result)[1].tags[0].value == "Game 2");
}

TEST_CASE("UTF-8 player names in tags", "[parser]")
{
    constexpr std::string_view pgn = R"(
[Event "?"][Site "Zürich"][Date "?"][Round "?"]
[White "Capablanca, José Raúl"][Black "Réti, Richard"][Result "1/2-1/2"]

1/2-1/2
)";

    auto result = pgn::parse_string(pgn);
    REQUIRE(result.has_value());
    CHECK(result->front().tags[1].value == "Zürich");
    CHECK(result->front().tags[4].value == "Capablanca, José Raúl");
    CHECK(result->front().tags[5].value == "Réti, Richard");
    CHECK(result->front().result == pgn::result::draw);
}

TEST_CASE("backslash-escaped quotes and backslashes in tag values", "[parser]")
{
    constexpr std::string_view pgn = R"(
[Event "He said \"hello\""][Site "C:\\Games\\PGN"][Date "?"][Round "?"]
[White "?"][Black "?"][Result "*"]

*
)";

    auto result = pgn::parse_string(pgn);
    REQUIRE(result.has_value());
    CHECK(result->front().tags[0].value == "He said \"hello\"");
    CHECK(result->front().tags[1].value == "C:\\Games\\PGN");
}

TEST_CASE("file_not_found error on missing file", "[parser]")
{
    auto result = pgn::parse_file("/nonexistent/path/game.pgn");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == pgn::parse_error::file_not_found);
}

// ─── Zukertort vs Steinitz, World Championship 1886, game 9 ───────────────────
//
// 38-move game (76 half-moves) with comments, NAGs ($19), variations nested up
// to three levels deep, castling (O-O), multi-line comments, and empty comments.

static const auto zukertort_path = test_data_dir / "zukertort_steinitz_1886.pgn";

TEST_CASE("Zukertort vs Steinitz 1886 - metadata", "[parser][zukertort]")
{
    auto result = pgn::parse_file(zukertort_path);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);

    auto const& game = result->front();
    CHECK(game.result == pgn::result::black);
    CHECK(game.tags.size() == 9);
    CHECK(game.tags[0].key == "Event");
    CHECK(game.tags[0].value == "Wch1");
    CHECK(game.tags[4].key == "White");
    CHECK(game.tags[4].value == "Zukertort, Johannes");
    CHECK(game.tags[5].key == "Black");
    CHECK(game.tags[5].value == "Steinitz, Wilhelm");
    CHECK(game.tags[8].key == "Annotator");
    CHECK(game.tags[8].value == "JvR");
}

TEST_CASE("Zukertort vs Steinitz 1886 - move sequence", "[parser][zukertort]")
{
    auto result = pgn::parse_file(zukertort_path);
    REQUIRE(result.has_value());

    auto const& moves = result->front().moves;
    REQUIRE(moves.size() == 76);  // 38 full moves = 76 half-moves

    CHECK(moves[0].number == 1);
    CHECK(moves[0].san == "d4");
    CHECK(moves[1].number == 0);   // black reply carries no move number token
    CHECK(moves[1].san == "d5");
    CHECK(moves[2].number == 2);
    CHECK(moves[2].san == "c4");

    CHECK(moves[14].san == "O-O"); // 8.O-O
    CHECK(moves[15].san == "O-O"); // 8...O-O

    CHECK(moves[20].san == "Bf4");
    CHECK(moves[21].san == "Nbd5");
    CHECK(moves[21].number == 11); // parsed from "11..."

    CHECK(moves[32].number == 17); // "17.\nBh4" split across a line break
    CHECK(moves[32].san == "Bh4");

    CHECK(moves[74].san == "Rxe4");
    CHECK(moves[75].san == "Qxe4");
}

TEST_CASE("Zukertort vs Steinitz 1886 - comments", "[parser][zukertort]")
{
    auto result = pgn::parse_file(zukertort_path);
    REQUIRE(result.has_value());

    auto const& moves = result->front().moves;

    REQUIRE(moves[17].comment.has_value());
    CHECK(moves[17].comment->find("blockades") != std::string::npos);

    REQUIRE(moves[32].comment.has_value());
    CHECK(moves[32].comment->find("18.Nxd5") != std::string::npos);

    REQUIRE(moves[35].comment.has_value());
    CHECK(moves[35].comment->find("hanging pawns") != std::string::npos);

    REQUIRE(moves[40].comment.has_value());
    CHECK(*moves[40].comment == "?!");

    REQUIRE(moves[75].comment.has_value());
    CHECK(moves[75].comment->find("New Orleans") != std::string::npos);

    // Empty comment {} inside the 25...fxe5 variation
    auto const& fxe5_var = moves[49].variations[0].moves;
    auto const& qh7 = fxe5_var.back();
    CHECK(qh7.san == "Qh7");
    REQUIRE(qh7.comment.has_value());
    CHECK(qh7.comment->empty());
}

TEST_CASE("Zukertort vs Steinitz 1886 - variations", "[parser][zukertort]")
{
    auto result = pgn::parse_file(zukertort_path);
    REQUIRE(result.has_value());

    auto const& moves = result->front().moves;

    // 11.Bf4 has one variation: ( 11.Re1 {keeps the initiative.} )
    REQUIRE(moves[20].variations.size() == 1);
    auto const& re1_var = moves[20].variations[0].moves;
    REQUIRE(re1_var.size() == 1);
    CHECK(re1_var[0].san == "Re1");
    CHECK(re1_var[0].number == 11);
    REQUIRE(re1_var[0].comment.has_value());
    CHECK(re1_var[0].comment->find("initiative") != std::string::npos);

    // 25...h6 has a variation that itself contains a nested variation
    REQUIRE(moves[49].variations.size() == 1);
    auto const& fxe5_var = moves[49].variations[0].moves;
    CHECK(fxe5_var[0].san == "fxe5");
    auto const* rd7 = [&]() -> pgn::move_node const* {
        for (auto const& mv : fxe5_var)
            if (mv.san == "Rd7") return &mv;
        return nullptr;
    }();
    REQUIRE(rd7 != nullptr);
    REQUIRE(rd7->variations.size() == 1);
    CHECK(rd7->variations[0].moves[0].san == "Rc7");

    // 34.Rxe6 has a variation: ( 34.Qxe6 Rc1+ $19 )
    REQUIRE(moves[66].variations.size() == 1);
    auto const& qxe6_var = moves[66].variations[0].moves;
    REQUIRE(qxe6_var.size() == 2);
    CHECK(qxe6_var[0].san == "Qxe6");
    CHECK(qxe6_var[1].san == "Rc1+");
}

TEST_CASE("Zukertort vs Steinitz 1886 - NAGs", "[parser][zukertort]")
{
    auto result = pgn::parse_file(zukertort_path);
    REQUIRE(result.has_value());

    auto const& moves = result->front().moves;

    // ( 34.Qxe6 Rc1+ $19 )
    auto const& qxe6_var = moves[66].variations[0].moves;
    REQUIRE(qxe6_var[1].nags.size() == 1);
    CHECK(qxe6_var[1].nags[0].value == 19);

    // ( 35.Nf1 Qc7 $19 {!} )
    REQUIRE(moves[68].variations.size() == 1);
    auto const& nf1_var = moves[68].variations[0].moves;
    REQUIRE(nf1_var[1].san == "Qc7");
    REQUIRE(nf1_var[1].nags.size() == 1);
    CHECK(nf1_var[1].nags[0].value == 19);

    // ( 31.Nd1 f4 32.Rh3 e5 {!} 33.d5 Bd7 $19 )
    REQUIRE(moves[60].variations.size() == 1);
    auto const& nd1_var = moves[60].variations[0].moves;
    CHECK(nd1_var.back().san == "Bd7");
    REQUIRE(nd1_var.back().nags.size() == 1);
    CHECK(nd1_var.back().nags[0].value == 19);
}

// ─── game_stream ──────────────────────────────────────────────────────────────

static constexpr std::string_view two_games_pgn = R"([Event "Game 1"]
[Site "?"][Date "?"][Round "1"]
[White "A"][Black "B"][Result "1-0"]

1. e4 1-0

[Event "Game 2"]
[Site "?"][Date "?"][Round "2"]
[White "C"][Black "D"][Result "0-1"]

1. d4 0-1
)";

TEST_CASE("game_stream handles CRLF line endings", "[stream]")
{
    // Replace every \n with \r\n to simulate a Windows-exported PGN file.
    std::string crlf;
    for (char c : two_games_pgn)
        (c == '\n' ? (crlf += '\r', crlf += '\n') : (crlf += c));

    std::vector<pgn::game> games;
    for (auto& eg : pgn::game_stream{std::string_view{crlf}}) {
        REQUIRE(eg.has_value());
        games.push_back(std::move(*eg));
    }
    REQUIRE(games.size() == 2);
    CHECK(games[0].tags[0].value == "Game 1");
    CHECK(games[1].tags[0].value == "Game 2");
}

TEST_CASE("game_stream yields games one at a time from string", "[stream]")
{
    std::vector<pgn::game> games;
    for (auto& eg : pgn::game_stream{two_games_pgn}) {
        REQUIRE(eg.has_value());
        games.push_back(std::move(*eg));
    }
    REQUIRE(games.size() == 2);
    CHECK(games[0].tags[0].value == "Game 1");
    CHECK(games[0].result == pgn::result::white);
    CHECK(games[1].tags[0].value == "Game 2");
    CHECK(games[1].result == pgn::result::black);
}

TEST_CASE("game_stream yields games from file", "[stream]")
{
    std::vector<pgn::game> games;
    for (auto& eg : pgn::game_stream{zukertort_path}) {
        REQUIRE(eg.has_value());
        games.push_back(std::move(*eg));
    }
    REQUIRE(games.size() == 1);
    CHECK(games[0].result == pgn::result::black);
    CHECK(games[0].moves.size() == 76);
}

TEST_CASE("game_stream reports file_not_found for missing file", "[stream]")
{
    std::vector<tl::expected<pgn::game, pgn::parse_error>> items;
    for (auto& eg : pgn::game_stream{std::filesystem::path{"/nonexistent/game.pgn"}})
        items.push_back(eg);

    REQUIRE(items.size() == 1);
    REQUIRE_FALSE(items[0].has_value());
    CHECK(items[0].error() == pgn::parse_error::file_not_found);
}

// A PGN missing a result token — clearly malformed, grammar requires one.
static constexpr std::string_view three_games_bad_middle = R"([Event "Good 1"]
[Site "?"][Date "?"][Round "1"]
[White "A"][Black "B"][Result "1-0"]

1. e4 1-0

[Event "Bad — no result token"]
[Site "?"][Date "?"][Round "2"]
[White "C"][Black "D"][Result "1-0"]

1. e4 e5

[Event "Good 2"]
[Site "?"][Date "?"][Round "3"]
[White "E"][Black "F"][Result "0-1"]

1. d4 0-1
)";

TEST_CASE("game_stream skips malformed game and continues", "[stream]")
{
    std::vector<tl::expected<pgn::game, pgn::parse_error>> items;
    for (auto& eg : pgn::game_stream{three_games_bad_middle})
        items.push_back(eg);

    REQUIRE(items.size() == 3);
    REQUIRE(items[0].has_value());
    CHECK(items[0]->tags[0].value == "Good 1");
    REQUIRE_FALSE(items[1].has_value());
    CHECK(items[1].error() == pgn::parse_error::syntax_error);
    REQUIRE(items[2].has_value());
    CHECK(items[2]->tags[0].value == "Good 2");
}

TEST_CASE("game_stream handles indented % comment between games", "[stream]")
{
    // Whitespace-indented % comment in the inter-game gap must not break splitting.
    constexpr std::string_view pgn = "[Event \"G1\"]\n"
                                     "[Site \"?\"][Date \"?\"][Round \"1\"]\n"
                                     "[White \"A\"][Black \"B\"][Result \"1-0\"]\n"
                                     "\n"
                                     "1. e4 1-0\n"
                                     "\n"
                                     "    % exported by tool\n"
                                     "[Event \"G2\"]\n"
                                     "[Site \"?\"][Date \"?\"][Round \"2\"]\n"
                                     "[White \"C\"][Black \"D\"][Result \"0-1\"]\n"
                                     "\n"
                                     "1. d4 0-1\n";

    std::vector<pgn::game> games;
    for (auto& eg : pgn::game_stream{pgn}) {
        REQUIRE(eg.has_value());
        games.push_back(std::move(*eg));
    }
    REQUIRE(games.size() == 2);
    CHECK(games[0].tags[0].value == "G1");
    CHECK(games[1].tags[0].value == "G2");
}

TEST_CASE("parse_string rejects leading garbage", "[parser]")
{
    auto r = pgn::parse_string("garbage");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == pgn::parse_error::syntax_error);
}

TEST_CASE("parse_string diagnostics overload captures message, silent overload has no output", "[parser]")
{
    std::string diag;
    auto r = pgn::parse_string("garbage", diag);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == pgn::parse_error::syntax_error);
    CHECK_FALSE(diag.empty());  // lexy wrote something about the parse failure
    CHECK(diag.find("EOF") != std::string::npos);

    // Silent overload must not touch the string.
    std::string silent_diag = "unchanged";
    auto r2 = pgn::parse_string("garbage");
    (void)r2;
    CHECK(silent_diag == "unchanged");
}

TEST_CASE("parse_string rejects trailing garbage", "[parser]")
{
    auto r = pgn::parse_string(std::string(minimal_pgn) + "\ngarbage\n");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == pgn::parse_error::syntax_error);
}

TEST_CASE("game_stream iterator byte_offset", "[stream]")
{
    pgn::game_stream stream{two_games_pgn};
    auto it = stream.begin();

    CHECK(it.byte_offset() == 0);       // first game starts at byte 0

    ++it;
    auto off2 = it.byte_offset();
    CHECK(off2 > 0);
    CHECK(off2 < two_games_pgn.size());
    CHECK(two_games_pgn[off2] == '[');  // offset lands on the opening tag bracket
}

// ─── 100K-game throughput (design.md: 100K games < 10 s) ─────────────────────

namespace {

std::string generate_synthetic_pgn(int n)
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

TEST_CASE("100K synthetic games parsed under 10 seconds", "[.][throughput]")
{
    static constexpr int num_games = 100'000;
    auto const pgn = generate_synthetic_pgn(num_games);

    auto const start = std::chrono::steady_clock::now();

    int count = 0;
    int errors = 0;
    for (auto& eg : pgn::game_stream{std::string_view{pgn}}) {
        if (eg.has_value())
            ++count;
        else
            ++errors;
    }

    auto const elapsed = std::chrono::steady_clock::now() - start;
    auto const seconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
        / 1000.0;

    INFO("Parsed " << count << " games in " << seconds << " s");
    CHECK(errors == 0);
    CHECK(count == num_games);
    CHECK(seconds < 10.0);
}
