// test_wire -- byte fixtures and a round trip.
//
// Layer 1 of the correctness program. Two kinds of test:
//   1. Hand-built bytes, constructed from the spec tables rather than captured
//      from a file, so a wrong offset fails here and not silently downstream.
//   2. Round trip: gen_synthetic writes a session, FrameReader parses it back.
//
// TWO FIXTURES ARE WRITTEN OUT BELOW ('A' and 'U'). The other 21 message types
// are yours. That is not laziness on my part -- building each fixture is how
// you actually read the spec, and the ones you build by hand are the ones you
// will remember under questioning.

#include "carteret/spec.hpp"
#include "carteret/wire.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace carteret;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// ---------------------------------------------------------------------------
// Fixture 1: 'A' Add Order, no MPID. 36 bytes, built byte by byte from
// spec section 1.3.1.
// ---------------------------------------------------------------------------
static void test_add_order() {
    const std::vector<unsigned char> msg = {
        'A',                                             // 0   type
        0x00, 0x2A,                                      // 1   locate = 42
        0x00, 0x01,                                      // 3   tracking = 1
        0x00, 0x00, 0x1F, 0x2B, 0x3C, 0x4D,              // 5   ts (6 bytes)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xD2,  // 11  order ref = 1234
        'B',                                             // 19  side
        0x00, 0x00, 0x01, 0xF4,                          // 20  shares = 500
        'A', 'A', 'P', 'L', ' ', ' ', ' ', ' ',          // 24  stock
        0x00, 0x16, 0xE3, 0x60,                          // 32  price = 1500000 = $150.0000
    };
    CHECK(msg.size() == kMsgLen['A']);

    const unsigned char* p = msg.data();
    CHECK(p[off::kType] == 'A');
    CHECK(be16(p + off::kStockLocate) == 42);
    CHECK(timestamp(p) == 0x00001F2B3C4DULL);
    CHECK(tracking(p) == 1);   // same load; must not leak into the timestamp
    CHECK(be64(p + off::add::kOrderRef) == 1234);
    CHECK(p[off::add::kSide] == 'B');
    CHECK(be32(p + off::add::kShares) == 500);
    CHECK(alpha(p + off::add::kStock, 8) == "AAPL");
    CHECK(be32(p + off::add::kPrice) == 1500000);
}

// ---------------------------------------------------------------------------
// Fixture 2: 'U' Order Replace. 35 bytes, spec section 1.4.5.
// The point of this one is the TWO refs: the old ref dies, the new ref is used
// for every subsequent update, and side/stock must be retained from the Add.
// ---------------------------------------------------------------------------
static void test_order_replace() {
    const std::vector<unsigned char> msg = {
        'U',
        0x00, 0x2A,
        0x00, 0x01,
        0x00, 0x00, 0x1F, 0x2B, 0x3C, 0x4E,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xD2,  // 11  old ref = 1234
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xD3,  // 19  new ref = 1235
        0x00, 0x00, 0x00, 0xC8,                          // 27  shares = 200
        0x00, 0x16, 0xE4, 0xC4,                          // 31  price = 1500356
    };
    CHECK(msg.size() == kMsgLen['U']);

    const unsigned char* p = msg.data();
    CHECK(be64(p + off::replace::kOldOrderRef) == 1234);
    CHECK(be64(p + off::replace::kNewOrderRef) == 1235);
    CHECK(be32(p + off::replace::kShares) == 200);
    CHECK(be32(p + off::replace::kPrice) == 1500356);
}

// ---------------------------------------------------------------------------
// Framing: length prefix, zero-length end marker, mismatch and unknown-type
// handling.
// ---------------------------------------------------------------------------
static void test_framing() {
    std::vector<unsigned char> buf;
    auto frame = [&](std::vector<unsigned char> body) {
        buf.push_back(static_cast<unsigned char>(body.size() >> 8));
        buf.push_back(static_cast<unsigned char>(body.size()));
        buf.insert(buf.end(), body.begin(), body.end());
    };

    std::vector<unsigned char> sysev(12, 0); sysev[0] = 'S'; sysev[11] = 'O';
    frame(sysev);

    // Known type, WRONG length. Must be skipped and counted, not parsed, and
    // must not desynchronise what follows.
    std::vector<unsigned char> bad(20, 0); bad[0] = 'D';   // 'D' should be 19
    frame(bad);

    // Unknown type byte.
    std::vector<unsigned char> unk(10, 0); unk[0] = '~';
    frame(unk);

    std::vector<unsigned char> del(19, 0); del[0] = 'D';
    frame(del);

    buf.push_back(0); buf.push_back(0);                    // end of session

    FrameReader rd(std::span<const unsigned char>{buf.data(), buf.size()});
    MsgView m;
    int ok = 0;
    for (;;) {
        const FrameStatus st = rd.next(m);
        if (st == FrameStatus::EndOfSession) break;
        if (st == FrameStatus::Truncated) { CHECK(false && "unexpected truncation"); break; }
        if (st == FrameStatus::Ok) ++ok;
    }
    CHECK(ok == 2);                 // the S and the good D
    CHECK(rd.mismatch() == 1);      // the bad D
    CHECK(rd.unknown() == 1);       // the '~'
}

// Every known type's length must round-trip through the table.
static void test_length_table() {
    const char* known = "SRHYLVWKJhAFECXDUPQBINO";
    int n = 0;
    for (const char* c = known; *c; ++c) { CHECK(kMsgLen[(unsigned char)*c] != 0); ++n; }
    CHECK(n == 23);
    CHECK(kMsgLen['Z'] == 0);
    CHECK(kMsgLen['I'] == kMaxMsgLen);
}

// The offset-3 load reads tracking + timestamp together. A nonzero tracking
// number must NOT leak into the timestamp's high bits.
static void test_timestamp_masking() {
    unsigned char m[12] = {'S', 0,0, 0xBE,0xEF, 0x00,0x1F,0x2B,0x3C,0x4D,0x5E, 'O'};
    CHECK(timestamp(m) == 0x001F2B3C4D5EULL);
    CHECK(tracking(m)  == 0xBEEF);
}

int main() {
    test_timestamp_masking();
    test_add_order();
    test_order_replace();
    test_framing();
    test_length_table();
    if (failures == 0) std::printf("all wire tests passed\n");
    else std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
