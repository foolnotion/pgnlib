#ifndef PGNLIB_TYPES_HPP
#define PGNLIB_TYPES_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pgn {

using u8 = std::uint8_t;

enum class result : u8 { white, black, draw, unknown };

enum class parse_error : u8 {
    file_not_found,
    syntax_error,
};

struct nag {
    int value;
};

struct tag {
    std::string key;
    std::string value;
};

// Forward-declared so variation and move_node can reference each other.
struct move_node;

struct variation {
    std::vector<move_node> moves;
};

struct move_node {
    int number;                        // 0 when no explicit move number present
    std::string san;                   // e.g. "e4", "Nf3", "O-O", "--"
    std::optional<std::string> comment;
    std::vector<nag> nags;
    std::vector<variation> variations;
};

struct game {
    std::vector<tag> tags;
    std::vector<move_node> moves;
    result result;
};

} // namespace pgn

#endif // PGNLIB_TYPES_HPP
