#ifndef PGNLIB_IMPORT_HPP
#define PGNLIB_IMPORT_HPP

// ── Import-oriented streaming API ─────────────────────────────────────────────
//
// import_stream is a lower-allocation alternative to game_stream for consumers
// that process moves incrementally and do not need the full pgn::game object.
//
// Key differences from pgn::game / game_stream:
//   • All string fields (tag keys, tag values, SAN) are std::string_view into
//     the source buffer — no heap allocation for string data.
//   • Tag values are raw bytes between the outer quotes; backslash escape
//     sequences (e.g. \") appear verbatim and are NOT decoded.  Callers that
//     need canonical values should unescape manually or use game_stream instead.
//   • Only the main line is produced.  Variations, comments, and NAGs are
//     consumed and silently discarded, keeping parsing cost proportional to the
//     mainline length.
//
// String_view lifetime: all views are valid as long as the import_stream
// (or the string_view passed to its constructor) is alive.

#include <cstddef>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string_view>
#include <vector>

#include <tl/expected.hpp>

#include "pgnlib/pgnlib_export.hpp"
#include "pgnlib/types.hpp"
// parse_error is defined in types.hpp (shared between pgnlib.hpp and import.hpp)

namespace pgn {

struct PGNLIB_EXPORT import_tag {
    std::string_view key;
    std::string_view value;  // raw bytes — backslash escapes NOT decoded
};

struct PGNLIB_EXPORT import_move {
    int              number;  // 0 when no explicit move-number token present
    std::string_view san;     // e.g. "e4", "Nf3", "O-O", "--"
};

struct PGNLIB_EXPORT import_game {
    std::vector<import_tag>  tags;
    std::vector<import_move> moves;
    pgn::result              result{pgn::result::unknown};
};

// ─────────────────────────────────────────────────────────────────────────────

class PGNLIB_EXPORT import_stream {
    struct impl;

public:
    // File overload: reads the whole file into an internal buffer.
    explicit import_stream(std::filesystem::path const& path);
    // String_view overload: borrows the caller's buffer (must outlive the stream).
    explicit import_stream(std::string_view input);

    ~import_stream();
    import_stream(import_stream&&) noexcept;
    import_stream& operator=(import_stream&&) noexcept;

    import_stream(import_stream const&)            = delete;
    import_stream& operator=(import_stream const&) = delete;

    struct PGNLIB_EXPORT iterator {
        using value_type        = tl::expected<import_game, parse_error>;
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
        std::size_t byte_offset() const noexcept;

    private:
        friend class import_stream;
        impl* impl_ = nullptr;
        explicit iterator(impl* p) noexcept : impl_(p) {}
    };

    iterator begin();
    std::default_sentinel_t end() const noexcept { return {}; }

private:
    std::unique_ptr<impl> impl_;
};

} // namespace pgn

#endif // PGNLIB_IMPORT_HPP
