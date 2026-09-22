// tests/itch_builder.hpp -- byte-level ITCH 5.0 message construction for tests.
//
// Messages are written out field by field from the specification tables rather
// than captured from a session file, so a test asserts against the
// specification and not against whatever the parser happened to produce.
//
// Test-only: this header is not part of the library and nothing in include/
// depends on it.

#pragma once

#include "carteret/spec.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace carteret::test {

// Every emitter is big-endian, matching the wire.
struct Bytes {
    std::vector<unsigned char> b;

    void u8(unsigned char v) { b.push_back(v); }
    void u16(std::uint16_t v) {
        b.push_back(static_cast<unsigned char>(v >> 8));
        b.push_back(static_cast<unsigned char>(v));
    }
    void u32(std::uint32_t v) {
        for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<unsigned char>(v >> s));
    }
    void u48(std::uint64_t v) {
        for (int s = 40; s >= 0; s -= 8) b.push_back(static_cast<unsigned char>(v >> s));
    }
    void u64(std::uint64_t v) {
        for (int s = 56; s >= 0; s -= 8) b.push_back(static_cast<unsigned char>(v >> s));
    }
    // Alpha fields are left justified and space padded on the right.
    void alpha(std::string_view s, std::size_t n) {
        std::size_t i = 0;
        for (; i < n && i < s.size(); ++i) b.push_back(static_cast<unsigned char>(s[i]));
        for (; i < n; ++i) b.push_back(' ');
    }

    [[nodiscard]] const unsigned char* data() const noexcept { return b.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return b.size(); }
};

// Writes the 11-byte common header. The tracking number is nonzero by default
// because it shares its load with the timestamp (docs/design.md record 002),
// so a zero would hide a masking error.
inline Bytes header(char type, std::uint16_t locate, std::uint64_t ts,
                    std::uint16_t tracking = 0xBEEF) {
    Bytes m;
    m.u8(static_cast<unsigned char>(type));
    m.u16(locate);
    m.u16(tracking);
    m.u48(ts);
    return m;
}

// Appends a message to a BinaryFILE-framed buffer: 2-byte big-endian length,
// then the body.
inline void frame(std::vector<unsigned char>& buf, const Bytes& body) {
    buf.push_back(static_cast<unsigned char>(body.size() >> 8));
    buf.push_back(static_cast<unsigned char>(body.size()));
    buf.insert(buf.end(), body.b.begin(), body.b.end());
}

// The zero-length prefix that terminates a session.
inline void frame_end(std::vector<unsigned char>& buf) {
    buf.push_back(0);
    buf.push_back(0);
}

} // namespace carteret::test
