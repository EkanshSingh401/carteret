// carteret/sha256.hpp -- SHA-256, implemented here rather than depended upon.
//
// Used for the determinism layer: a hash over the reconstructed event stream
// and over the final book state, committed as a golden value so that an
// unexplained change fails CI (docs/correctness.md, layer 5).
//
// Implemented in-tree, in one header, with no external dependency. The
// alternative was vendoring a single-file implementation, which would carry
// someone else's licence into the tree for 200 lines of well-specified
// arithmetic. Correctness is established the same way either choice would
// require: `tests/test_sha256.cpp` checks the FIPS 180-4 example digests, the
// empty-string digest, a multi-block message, and every length from 0 to 200
// bytes against an incremental-versus-one-shot comparison, so a padding or
// length-encoding error at any block boundary fails.
//
// This is a checksum for change detection, not a security boundary. Nothing
// here is constant-time and nothing needs to be.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace carteret {

class Sha256 {
public:
    static constexpr std::size_t kDigestBytes = 32;
    using Digest = std::array<std::uint8_t, kDigestBytes>;

    Sha256() = default;

    void update(const void* data, std::size_t len) noexcept {
        const auto* p = static_cast<const std::uint8_t*>(data);
        bit_len_ += static_cast<std::uint64_t>(len) * 8u;
        while (len > 0) {
            const std::size_t take = (64 - buf_len_ < len) ? (64 - buf_len_) : len;
            std::memcpy(buf_.data() + buf_len_, p, take);
            buf_len_ += take;
            p += take;
            len -= take;
            if (buf_len_ == 64) {
                block(buf_.data());
                buf_len_ = 0;
            }
        }
    }

    // Convenience for the fixed-width values the event stream is built from.
    void update_u64(std::uint64_t v) noexcept {
        std::uint8_t b[8];
        for (int i = 0; i < 8; ++i) b[i] = static_cast<std::uint8_t>(v >> (56 - 8 * i));
        update(b, 8);
    }
    void update_u32(std::uint32_t v) noexcept {
        std::uint8_t b[4];
        for (int i = 0; i < 4; ++i) b[i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
        update(b, 4);
    }
    void update_u8(std::uint8_t v) noexcept { update(&v, 1); }

    [[nodiscard]] Digest finish() const noexcept {
        // Works on a copy, so a caller can hash an intermediate state and keep
        // going. The alternative -- consuming the object -- would force the
        // periodic book hashes to rebuild from scratch each time.
        Sha256 tmp = *this;
        std::uint8_t pad[72] = {0x80};
        const std::size_t pad_len =
            (tmp.buf_len_ < 56) ? (56 - tmp.buf_len_) : (120 - tmp.buf_len_);
        tmp.update_raw(pad, pad_len);
        std::uint8_t len_be[8];
        for (int i = 0; i < 8; ++i) {
            len_be[i] = static_cast<std::uint8_t>(tmp.bit_len_ >> (56 - 8 * i));
        }
        tmp.update_raw(len_be, 8);

        Digest out{};
        for (int i = 0; i < 8; ++i) {
            out[static_cast<std::size_t>(i * 4 + 0)] =
                static_cast<std::uint8_t>(tmp.h_[static_cast<std::size_t>(i)] >> 24);
            out[static_cast<std::size_t>(i * 4 + 1)] =
                static_cast<std::uint8_t>(tmp.h_[static_cast<std::size_t>(i)] >> 16);
            out[static_cast<std::size_t>(i * 4 + 2)] =
                static_cast<std::uint8_t>(tmp.h_[static_cast<std::size_t>(i)] >> 8);
            out[static_cast<std::size_t>(i * 4 + 3)] =
                static_cast<std::uint8_t>(tmp.h_[static_cast<std::size_t>(i)]);
        }
        return out;
    }

    [[nodiscard]] std::string hex() const {
        static constexpr char kHex[] = "0123456789abcdef";
        const Digest d = finish();
        std::string s(kDigestBytes * 2, '0');
        for (std::size_t i = 0; i < kDigestBytes; ++i) {
            s[i * 2] = kHex[d[i] >> 4];
            s[i * 2 + 1] = kHex[d[i] & 0x0F];
        }
        return s;
    }

    [[nodiscard]] static std::string hex_of(const void* data, std::size_t len) {
        Sha256 s;
        s.update(data, len);
        return s.hex();
    }

private:
    // Buffers without touching the length counter, for padding during finish.
    void update_raw(const std::uint8_t* p, std::size_t len) noexcept {
        while (len > 0) {
            const std::size_t take = (64 - buf_len_ < len) ? (64 - buf_len_) : len;
            std::memcpy(buf_.data() + buf_len_, p, take);
            buf_len_ += take;
            p += take;
            len -= take;
            if (buf_len_ == 64) {
                block(buf_.data());
                buf_len_ = 0;
            }
        }
    }

    static std::uint32_t ror(std::uint32_t x, int n) noexcept {
        return (x >> n) | (x << (32 - n));
    }

    void block(const std::uint8_t* p) noexcept {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};

        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(p[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        std::uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h_[0] += a;
        h_[1] += b;
        h_[2] += c;
        h_[3] += d;
        h_[4] += e;
        h_[5] += f;
        h_[6] += g;
        h_[7] += hh;
    }

    std::array<std::uint32_t, 8> h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<std::uint8_t, 64> buf_{};
    std::size_t buf_len_ = 0;
    std::uint64_t bit_len_ = 0;
};

} // namespace carteret
