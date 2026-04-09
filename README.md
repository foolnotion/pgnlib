# pgnlib

PGN parser library for chess applications, built on
[foonathan/lexy](https://lexy.foonathan.net/).

Produces `pgn::game` structs with raw SAN strings — no chess logic dependency.
SAN validation is the consumer's responsibility.

## Features

- **Lexy grammar** — tags, SAN tokens, NAGs (`$0`–`$255`), result tokens
- **Recursive variations** — tested to depth ≥ 10
- **Comments** — `{text}` blocks, before and after NAGs
- **Null moves** — `--`
- **`%` line comments** — Lichess `%clk` annotations skipped automatically
- **UTF-8** — full code-point support in tag values (player names, sites)
- **Streaming** — `game_stream` lazily yields one game at a time
- **Error recovery** — malformed games are skipped with `parse_error`; stream continues

## API

```cpp
#include <pgnlib/pgnlib.hpp>

// Eager — parse everything at once
auto games = pgn::parse_file("games.pgn");   // -> tl::expected<vector<game>, parse_error>
auto games = pgn::parse_string(pgn_text);    // -> tl::expected<vector<game>, parse_error>

// Streaming — one game at a time, constant memory
for (auto& eg : pgn::game_stream{"games.pgn"}) {
    if (eg) process(*eg);
    else    log_error(eg.error());
}
```

## Building

See [BUILDING.md](BUILDING.md) for full instructions.

Quick start with Nix:

```sh
nix develop
cmake -B build -Dpgnlib_DEVELOPER_MODE=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## License

[MIT](LICENSE)
