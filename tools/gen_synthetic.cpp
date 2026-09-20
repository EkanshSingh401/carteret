// gen_synthetic -- writes a deterministic BinaryFILE-framed ITCH 5.0 session.
//
// Why this exists: the real sessions are 5-13 GB and cannot be committed. CI
// needs something to run against, and hand-built bytes need a round-trip
// partner. This writer is the partner: write a known stream, parse it back,
// assert equality.
//
// It is deliberately DUMB. It does not model a market, does not maintain a
// book, and its order flow is not realistic. Do not benchmark against it and
// do not draw microstructure conclusions from it -- that is what the NASDAQ
// sessions are for. Its only job is to exercise the framing and decode paths.
//
//   usage: gen_synthetic <out-file> <n-messages> [seed]

#include "carteret/spec.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace carteret;

namespace {

void put16(std::vector<unsigned char>& b, std::uint16_t v) {
    b.push_back(static_cast<unsigned char>(v >> 8));
    b.push_back(static_cast<unsigned char>(v));
}
void put32(std::vector<unsigned char>& b, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<unsigned char>(v >> s));
}
void put48(std::vector<unsigned char>& b, std::uint64_t v) {
    for (int s = 40; s >= 0; s -= 8) b.push_back(static_cast<unsigned char>(v >> s));
}
void put64(std::vector<unsigned char>& b, std::uint64_t v) {
    for (int s = 56; s >= 0; s -= 8) b.push_back(static_cast<unsigned char>(v >> s));
}
void putalpha(std::vector<unsigned char>& b, const char* s, std::size_t n) {
    std::size_t i = 0;
    for (; i < n && s[i]; ++i) b.push_back(static_cast<unsigned char>(s[i]));
    for (; i < n; ++i) b.push_back(' ');
}
void header(std::vector<unsigned char>& b, char type, std::uint16_t locate, std::uint64_t ts) {
    b.push_back(static_cast<unsigned char>(type));
    put16(b, locate);
    put16(b, 0);          // tracking number
    put48(b, ts);
}
// Frame and emit: 2-byte big-endian length, then body.
void emit(std::FILE* f, const std::vector<unsigned char>& body) {
    unsigned char pfx[2] = {static_cast<unsigned char>(body.size() >> 8),
                            static_cast<unsigned char>(body.size())};
    std::fwrite(pfx, 1, 2, f);
    std::fwrite(body.data(), 1, body.size(), f);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <out-file> <n-messages> [seed]\n", argv[0]);
        return 2;
    }
    const std::string out = argv[1];
    const std::uint64_t n = std::strtoull(argv[2], nullptr, 10);
    const std::uint32_t seed = (argc > 3) ? static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 10)) : 42u;

    std::FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) { std::perror("fopen"); return 1; }

    std::mt19937 rng(seed);
    std::vector<unsigned char> b;
    std::uint64_t ts = 34200ULL * 1000000000ULL;   // 09:30:00
    std::uint64_t next_ref = 1;
    std::vector<std::uint64_t> live;
    live.reserve(1024);

    const char* syms[] = {"AAPL", "MSFT", "INTC"};

    // 'S' start of messages
    b.clear(); header(b, 'S', 0, ts); b.push_back('O'); emit(f, b);

    // 'R' stock directory, one per symbol
    for (std::uint16_t i = 0; i < 3; ++i) {
        b.clear(); header(b, 'R', static_cast<std::uint16_t>(i + 1), ts);
        putalpha(b, syms[i], 8);
        b.push_back('Q'); b.push_back('N');
        put32(b, 100);
        b.push_back('N'); b.push_back('C');
        putalpha(b, "", 2);
        b.push_back('P'); b.push_back('N'); b.push_back('N');
        b.push_back('1'); b.push_back('N');
        put32(b, 1);
        b.push_back('N');
        emit(f, b);
    }

    for (std::uint64_t i = 0; i < n; ++i) {
        ts += 1 + (rng() % 5000);
        const std::uint16_t locate = static_cast<std::uint16_t>(1 + rng() % 3);
        const std::uint32_t roll = static_cast<std::uint32_t>(rng() % 100);

        if (roll < 45 || live.empty()) {                  // Add
            const std::uint64_t ref = next_ref++;
            b.clear(); header(b, 'A', locate, ts);
            put64(b, ref);
            b.push_back((rng() % 2) ? 'B' : 'S');
            put32(b, static_cast<std::uint32_t>(100 * (1 + rng() % 10)));
            putalpha(b, syms[locate - 1], 8);
            put32(b, static_cast<std::uint32_t>(1000000 + (rng() % 20000) * 100));    // Price(4), penny ticks
            emit(f, b);
            live.push_back(ref);
        } else {
            const std::size_t idx = static_cast<std::size_t>(rng()) % live.size();
            const std::uint64_t ref = live[idx];
            if (roll < 60) {                              // Execute
                b.clear(); header(b, 'E', locate, ts);
                put64(b, ref); put32(b, 100); put64(b, i + 1);
                emit(f, b);
            } else if (roll < 70) {                       // Partial cancel
                b.clear(); header(b, 'X', locate, ts);
                put64(b, ref); put32(b, 100);
                emit(f, b);
            } else if (roll < 90) {                       // Delete
                b.clear(); header(b, 'D', locate, ts);
                put64(b, ref);
                emit(f, b);
                live[idx] = live.back(); live.pop_back();
            } else {                                      // Replace
                const std::uint64_t nref = next_ref++;
                b.clear(); header(b, 'U', locate, ts);
                put64(b, ref); put64(b, nref);
                put32(b, static_cast<std::uint32_t>(100 * (1 + rng() % 10)));
                put32(b, static_cast<std::uint32_t>(1000000 + (rng() % 20000) * 100));
                emit(f, b);
                live[idx] = nref;
            }
        }
    }

    // 'S' end of messages, then the zero-length end-of-session marker.
    b.clear(); header(b, 'S', 0, ts); b.push_back('C'); emit(f, b);
    unsigned char zero[2] = {0, 0};
    std::fwrite(zero, 1, 2, f);

    std::fclose(f);
    std::fprintf(stderr, "wrote %s\n", out.c_str());
    return 0;
}
