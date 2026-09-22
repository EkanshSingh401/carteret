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
    std::uint16_t len = 0;

    [[nodiscard]] unsigned char type() const noexcept { return data[off::kType]; }
    [[nodiscard]] std::uint16_t locate() const noexcept {
        return be16(data + off::kStockLocate);
    }
    [[nodiscard]] std::uint64_t ts() const noexcept { return timestamp(data); }
    [[nodiscard]] std::uint16_t track() const noexcept { return tracking(data); }
};

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------
//
// NASDAQ BinaryFILE precedes every message with a 2-byte big-endian length.
//
// A zero-length prefix as a file terminator is NOT part of the ITCH 5.0
// specification. The specification guarantees one thing about the end of a
// session: System Event 'C', End of Messages, is the last message of the day.
// The zero-length terminator is a third-party description of how the file is
// packaged, and the sessions NASDAQ publishes do not write one -- they end
// with the 'C' message and then the file ends.
//
// So this reader reports the two endings separately rather than treating them
// as one. Neither is authoritative on its own: whether a file is complete is
// decided by its last MESSAGE, which is a question about content and belongs
// to the census, not to the framing loop. A file that ends part-way through a
// message is still a truncation, which the framing loop can and does decide.
//
// One file in circulation does not: ex20101224.TEST_ITCH_50, bundled with the
// RITCH R package, carries the 2-byte prefix field for all 12,012 of its
// messages and leaves every one of them zero. The prefix is present and
// unfilled, so the length has to come from the type byte instead. The variant
// is handled explicitly rather than by relaxing the prefix rule for every
// file, because in the prefixed form a zero prefix is the end-of-session
// marker and the two readings cannot both be applied to the same bytes.
//
// In the prefixed form the prefix alone advances the cursor, and for known
// types it is additionally checked against the specification length in one
// table lookup. A frame whose length disagrees, or whose type is unknown, is
// skipped by its prefix length and counted, never parsed, so that one
// malformed frame cannot desynchronise the rest of the session.
//
// In the zero-prefixed form there is no independent length, so an unknown type
// byte is unrecoverable: nothing says how far to skip. The reader stops and
// reports a truncation rather than guessing. That asymmetry is the cost of a
// file format that does not carry its own lengths, and it is why the prefixed
// form is the one every result in this repository is produced from. See
// docs/design.md record 001.
//
// Detection is unaffected by the terminator question: it looks at whether the
// first frames' prefixes match their types' lengths, not at how the file
// ends.

enum class Framing : unsigned char {
    LengthPrefixed, // 2-byte big-endian length before each message
    ZeroPrefixed,   // 2-byte prefix present but always zero; length from the type byte
};

enum class FrameStatus : unsigned char {
    Ok,
    ZeroLengthPrefix, // an explicit zero-length prefix
    Exhausted,        // the buffer ended exactly at a message boundary
    Truncated,        // prefix or body runs past the end of the buffer
    LengthMismatch,   // known type, prefix length disagrees; skipped and counted
    UnknownType,      // not an ITCH 5.0 type; skipped and counted
};

class FrameReader {
public:
    explicit FrameReader(std::span<const unsigned char> buf,
                         Framing f = Framing::LengthPrefixed) noexcept
        : buf_(buf), framing_(f) {}

    // Advances by one frame. On Ok, `out` is populated. On LengthMismatch and
    // UnknownType the frame is consumed and skipped so the caller can continue;
    // in the zero-prefixed form UnknownType is terminal, because the length is
    // unknown and there is no safe distance to skip.
    FrameStatus next(MsgView& out) noexcept {
        return framing_ == Framing::LengthPrefixed ? next_prefixed(out)
                                                   : next_zero_prefixed(out);
    }

    [[nodiscard]] std::size_t offset() const noexcept { return pos_; }
    [[nodiscard]] std::uint64_t unknown() const noexcept { return unknown_; }
    [[nodiscard]] std::uint64_t mismatch() const noexcept { return mismatch_; }
    [[nodiscard]] Framing framing() const noexcept { return framing_; }

private:
    FrameStatus next_prefixed(MsgView& out) noexcept {
        // A buffer ending exactly at a message boundary is a well-formed end
        // of input. 20190130.BX_ITCH_50 ends this way: its last message is the
        // 'S' End of Messages event and then the file ends, with the reader's
        // cursor on the final byte. Whether that file is COMPLETE is a
        // separate question, answered by the last message rather than by the
        // absence of bytes; see the header comment.
        if (pos_ == buf_.size()) return FrameStatus::Exhausted;
        if (pos_ + 2 > buf_.size()) return FrameStatus::Truncated;

        const std::uint16_t len = be16(buf_.data() + pos_);
        if (len == 0) {
            pos_ += 2;
            return FrameStatus::ZeroLengthPrefix;
        }
        if (pos_ + 2 + len > buf_.size()) return FrameStatus::Truncated;

        const unsigned char* body = buf_.data() + pos_ + 2;
        pos_ += 2 + len;

        const unsigned char t = body[off::kType];
        const std::uint8_t expect = kMsgLen[t];
        if (expect == 0) {
            ++unknown_;
            return FrameStatus::UnknownType;
        }
        if (expect != len) {
            ++mismatch_;
            return FrameStatus::LengthMismatch;
        }

        out.data = body;
        out.len = len;
        return FrameStatus::Ok;
    }

    FrameStatus next_zero_prefixed(MsgView& out) noexcept {
        // A message boundary at exactly the end of the buffer is how this
        // variant ends; it carries no terminator of its own.
        if (pos_ == buf_.size()) return FrameStatus::Exhausted;
        if (pos_ + 3 > buf_.size()) return FrameStatus::Truncated;

        // The prefix field is expected to be zero. A nonzero value means the
        // framing was misidentified, so it is counted and the type-implied
        // length is used, which keeps the walk deterministic.
        if (be16(buf_.data() + pos_) != 0) ++mismatch_;

        const unsigned char* body = buf_.data() + pos_ + 2;
        const unsigned char t = body[off::kType];
        const std::uint8_t len = kMsgLen[t];
        if (len == 0) {
            ++unknown_;
            return FrameStatus::Truncated; // no length available; cannot resynchronise
        }
        if (pos_ + 2 + len > buf_.size()) return FrameStatus::Truncated;
        pos_ += 2 + len;

        out.data = body;
        out.len = len;
        return FrameStatus::Ok;
    }

    std::span<const unsigned char> buf_;
    Framing framing_ = Framing::LengthPrefixed;
    std::size_t pos_ = 0;
    std::uint64_t unknown_ = 0;
    std::uint64_t mismatch_ = 0;
};

// Identifies which of the two forms a buffer uses by walking up to kProbe
// messages under each hypothesis and taking the one that stays consistent.
// Ambiguity resolves to LengthPrefixed, which is the form every session in
// docs/data.md uses and the only one that can recover from a bad frame.
inline Framing detect_framing(std::span<const unsigned char> buf) noexcept {
    constexpr int kProbe = 64;

    auto walks = [&](Framing f) {
        std::size_t pos = 0;
        for (int i = 0; i < kProbe; ++i) {
            if (pos + 3 > buf.size()) return i > 0; // ran out cleanly after some progress
            const std::uint16_t pfx = be16(buf.data() + pos);
            const unsigned char t = buf[pos + 2];
            const std::uint8_t implied = kMsgLen[t];
            if (implied == 0) return false;
            if (f == Framing::LengthPrefixed) {
                if (pfx == 0) return i > 0; // end-of-session marker
                if (pfx != implied) return false;
            } else {
                if (pfx != 0) return false;
            }
            pos += 2u + implied;
            if (pos > buf.size()) return false;
        }
        return true;
    };

    if (walks(Framing::LengthPrefixed)) return Framing::LengthPrefixed;
    if (walks(Framing::ZeroPrefixed)) return Framing::ZeroPrefixed;
    return Framing::LengthPrefixed;
}

} // namespace carteret
