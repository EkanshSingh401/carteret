// carteret/wire.hpp -- BinaryFILE framing and big-endian field decode.
//
// Scope is two things: walk a memory-mapped session file yielding one message
// view at a time, and decode big-endian fields out of that view without
// undefined behaviour. Book construction, handler dispatch and memory ownership
// live elsewhere.
//
// Decoding goes through memcpy rather than a pointer cast because the 64-bit
// order reference sits at offset 11 in every order message, which is not
// 8-byte aligned. Casting a buffer pointer to `const uint64_t*` is undefined
// behaviour twice over: an unaligned load and a strict-aliasing violation. It
// happens to work on x86, which is what makes the bug durable. memcpy into a
// local plus __builtin_bswap64 costs nothing at -O2; see docs/design.md
// record 002 for the generated assembly and the command that produced it.

#pragma once

#include "spec.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace carteret {

// ---------------------------------------------------------------------------
// Big-endian field decode
// ---------------------------------------------------------------------------

[[gnu::always_inline]] inline std::uint16_t be16(const unsigned char* p) noexcept {
    std::uint16_t v;
    std::memcpy(&v, p, 2);
    return __builtin_bswap16(v);
}

[[gnu::always_inline]] inline std::uint32_t be32(const unsigned char* p) noexcept {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return __builtin_bswap32(v);
}

[[gnu::always_inline]] inline std::uint64_t be64(const unsigned char* p) noexcept {
    std::uint64_t v;
    std::memcpy(&v, p, 8);
    return __builtin_bswap64(v);
}

// Timestamp and tracking number come from a single 8-byte load.
//
// The 2-byte tracking number at offset 3 and the 6-byte timestamp at offset 5
// are exactly 8 contiguous bytes, so loading [3, 11) and byte-swapping once
// yields the timestamp in the low 48 bits and the tracking number in the high
// 16. The load cannot overread: the shortest ITCH 5.0 message is 12 bytes and
// the load ends at offset 11.
//
// The alternative -- copying 6 bytes into a zeroed u64 -- compiled to roughly
// 16 instructions with a stack spill under GCC 13.3 -O2. See docs/design.md
// record 002.
//
// These take the message base pointer, not a field pointer.
[[gnu::always_inline]] inline std::uint64_t header_tail(const unsigned char* msg) noexcept {
    std::uint64_t v;
    std::memcpy(&v, msg + off::kTracking, 8);
    return __builtin_bswap64(v);
}
[[gnu::always_inline]] inline std::uint64_t timestamp(const unsigned char* msg) noexcept {
    return header_tail(msg) & 0x0000FFFFFFFFFFFFULL;
}
[[gnu::always_inline]] inline std::uint16_t tracking(const unsigned char* msg) noexcept {
    return static_cast<std::uint16_t>(header_tail(msg) >> 48);
}

// Alpha fields are space-padded on the right. Returns a view into the buffer
// with the padding stripped; no allocation and no copy.
inline std::string_view alpha(const unsigned char* p, std::size_t n) noexcept {
    while (n > 0 && p[n - 1] == ' ') --n;
    return {reinterpret_cast<const char*>(p), n};
}

// ---------------------------------------------------------------------------
// A view of one message. Non-owning; valid only while the buffer lives.
// ---------------------------------------------------------------------------

struct MsgView {
    const unsigned char* data = nullptr;
    std::uint16_t        len  = 0;

    [[nodiscard]] unsigned char   type()   const noexcept { return data[off::kType]; }
    [[nodiscard]] std::uint16_t   locate() const noexcept { return be16(data + off::kStockLocate); }
    [[nodiscard]] std::uint64_t   ts()     const noexcept { return timestamp(data); }
    [[nodiscard]] std::uint16_t   track()  const noexcept { return tracking(data); }
};

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------
//
// NASDAQ BinaryFILE: every message is preceded by a 2-byte big-endian length,
// and a zero length marks end of session.
//
// The prefix is authoritative for advancing through the file. For known types
// it is additionally checked against the specification length in one table
// lookup. A frame whose length disagrees, or whose type is unknown, is skipped
// by its prefix length and counted, never parsed, so that one malformed frame
// cannot desynchronise the rest of the session.

enum class FrameStatus : unsigned char {
    Ok,
    EndOfSession,     // zero-length prefix
    Truncated,        // prefix or body runs past the end of the buffer
    LengthMismatch,   // known type, wrong length; skipped and counted
    UnknownType,      // not an ITCH 5.0 type; skipped and counted
};

class FrameReader {
public:
    explicit FrameReader(std::span<const unsigned char> buf) noexcept : buf_(buf) {}

    // Advances by one frame. On Ok, `out` is populated. On LengthMismatch and
    // UnknownType the frame is consumed and skipped so the caller can continue.
    FrameStatus next(MsgView& out) noexcept {
        if (pos_ + 2 > buf_.size()) return FrameStatus::Truncated;

        const std::uint16_t len = be16(buf_.data() + pos_);
        if (len == 0) return FrameStatus::EndOfSession;
        if (pos_ + 2 + len > buf_.size()) return FrameStatus::Truncated;

        const unsigned char* body = buf_.data() + pos_ + 2;
        pos_ += 2 + len;

        const unsigned char t = body[off::kType];
        const std::uint8_t  expect = kMsgLen[t];
        if (expect == 0) { ++unknown_; return FrameStatus::UnknownType; }
        if (expect != len) { ++mismatch_; return FrameStatus::LengthMismatch; }

        out.data = body;
        out.len  = len;
        return FrameStatus::Ok;
    }

    [[nodiscard]] std::size_t offset()   const noexcept { return pos_; }
    [[nodiscard]] std::uint64_t unknown()  const noexcept { return unknown_; }
    [[nodiscard]] std::uint64_t mismatch() const noexcept { return mismatch_; }

private:
    std::span<const unsigned char> buf_;
    std::size_t   pos_      = 0;
    std::uint64_t unknown_  = 0;
    std::uint64_t mismatch_ = 0;
};

}  // namespace carteret
