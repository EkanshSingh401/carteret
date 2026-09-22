// test_sha256 -- the in-tree SHA-256 against the published digests.
//
// The determinism layer's golden hashes are only evidence if the hash is
// right. The failure this guards against is a padding or length-encoding error
// that happens to be consistent: a wrong-but-deterministic hash would pass
// every determinism check and agree with nothing outside this repository.

#include "carteret/sha256.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace carteret;

namespace {

int failures = 0;

#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
            ++failures;                                                                        \
        }                                                                                      \
    } while (0)

void expect(const std::string& in, const char* want, const char* label) {
    const std::string got = Sha256::hex_of(in.data(), in.size());
    if (got != want) {
        std::fprintf(stderr, "FAIL %s\n  got  %s\n  want %s\n", label, got.c_str(), want);
        ++failures;
    }
}

// FIPS 180-4 examples, plus the empty input and the one-million-byte case that
// exercises the 64-bit length field beyond a single block.
void test_published_vectors() {
    expect("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty");
    expect("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc");
    expect("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "two-block");
    expect("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
           "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
           "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1", "long");

    Sha256 million;
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) million.update(chunk.data(), chunk.size());
    const std::string got = million.hex();
    const char* want = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
    if (got != want) {
        std::fprintf(stderr, "FAIL one million 'a'\n  got  %s\n  want %s\n", got.c_str(), want);
        ++failures;
    }
}

// Feeding the same bytes in different-sized pieces must give the same digest.
// This is what catches a buffering error at a block boundary, which a
// one-shot test over a few fixed strings would miss.
void test_incremental_matches_one_shot() {
    std::string data;
    for (int i = 0; i < 200; ++i) data.push_back(static_cast<char>('a' + (i % 26)));

    for (std::size_t len = 0; len <= data.size(); ++len) {
        const std::string prefix = data.substr(0, len);
        const std::string one_shot = Sha256::hex_of(prefix.data(), prefix.size());
        for (std::size_t chunk : {std::size_t(1), std::size_t(7), std::size_t(63),
                                  std::size_t(64), std::size_t(65), std::size_t(128)}) {
            Sha256 s;
            for (std::size_t off = 0; off < prefix.size(); off += chunk) {
                s.update(prefix.data() + off, std::min(chunk, prefix.size() - off));
            }
            if (s.hex() != one_shot) {
                std::fprintf(stderr, "FAIL incremental len=%zu chunk=%zu\n", len, chunk);
                ++failures;
            }
        }
    }
}

// finish() must not consume the state: the differential harness hashes a
// running book periodically and keeps going.
void test_finish_is_not_destructive() {
    Sha256 s;
    s.update("abc", 3);
    const std::string first = s.hex();
    const std::string second = s.hex();
    CHECK(first == second);
    s.update("def", 3);
    CHECK(s.hex() == Sha256::hex_of("abcdef", 6));
}

// The fixed-width helpers must be big-endian and width-exact, so a digest
// taken on one host matches one taken on another.
void test_fixed_width_helpers() {
    Sha256 a;
    a.update_u32(0x01020304u);
    const unsigned char raw32[4] = {0x01, 0x02, 0x03, 0x04};
    CHECK(a.hex() == Sha256::hex_of(raw32, 4));

    Sha256 b;
    b.update_u64(0x0102030405060708ULL);
    const unsigned char raw64[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(b.hex() == Sha256::hex_of(raw64, 8));

    // A u32 and a u64 of the same value must differ, or width errors are
    // invisible to the golden hashes.
    Sha256 c, d;
    c.update_u32(1);
    d.update_u64(1);
    CHECK(c.hex() != d.hex());
}

} // namespace

int main() {
    test_published_vectors();
    test_incremental_matches_one_shot();
    test_finish_is_not_destructive();
    test_fixed_width_helpers();

    if (failures == 0) {
        std::printf("all sha256 tests passed\n");
    } else {
        std::printf("%d failure(s)\n", failures);
    }
    return failures ? 1 : 0;
}
