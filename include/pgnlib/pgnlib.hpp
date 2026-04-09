#ifndef PGNLIB_PGNLIB_HPP
#define PGNLIB_PGNLIB_HPP

#include <cstddef>
#include <filesystem>
#include <iterator>
#include <memory>
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

// ── Streaming parser ──────────────────────────────────────────────────────────
//
// game_stream is an input range that yields one tl::expected<game,parse_error>
// at a time.  The file overload reads the whole file into an internal buffer
// (simple and fast); the string_view overload borrows the caller's buffer
// (must outlive the stream).  In both cases only one parsed game object
// exists at a time — previous games are released when the iterator advances.
//
// Usage:
//   for (auto& eg : pgn::game_stream{path}) {
//       if (eg) process(*eg);
//   }

class PGNLIB_EXPORT game_stream {
    struct impl; // forward-declared before iterator so iterator can hold impl*

public:
    explicit game_stream(std::filesystem::path const& path);
    explicit game_stream(std::string_view input);

    ~game_stream();
    game_stream(game_stream&&) noexcept;
    game_stream& operator=(game_stream&&) noexcept;

    game_stream(game_stream const&)            = delete;
    game_stream& operator=(game_stream const&) = delete;

    struct PGNLIB_EXPORT iterator {
        using value_type        = tl::expected<game, parse_error>;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;
        using pointer           = value_type*;
        using reference         = value_type&;

        iterator() = default;

        reference   operator*()  noexcept;
        pointer     operator->() noexcept;
        iterator&   operator++();
        void        operator++(int) { ++(*this); }
        bool        operator==(std::default_sentinel_t) const noexcept;

        // Byte offset of the current game's opening '[' in the source buffer.
        // Useful for logging parse errors with file positions and for
        // recording resume-points in long imports.
        std::size_t byte_offset() const noexcept;

    private:
        friend class game_stream;
        impl* impl_ = nullptr;      // non-owning; game_stream owns the impl
        explicit iterator(impl* p) noexcept : impl_(p) {}
    };

    iterator begin();
    std::default_sentinel_t end() const noexcept { return {}; }

private:
    std::unique_ptr<impl> impl_;
};

} // namespace pgn

#endif // PGNLIB_PGNLIB_HPP
