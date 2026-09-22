// test_wire -- byte fixtures and framing tests.
//
// Correctness layer 1. Fixtures are built byte by byte from the specification
// tables rather than captured from a session file, so a transcription error in
// an offset fails here instead of propagating silently into the book. The
// round-trip half of layer 1 lives in tests/roundtrip.cmake, which parses back
// a session written by gen_synthetic.

#include "carteret/spec.hpp"
#include "carteret/wire.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace carteret;

static int failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
            ++failures;                                                                        \
        }                                                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// 'A' Add Order, no MPID. 36 bytes, specification section 1.3.1.
// ---------------------------------------------------------------------------
static void test_add_order() {
    const std::vector<unsigned char> msg = {
        'A',                                            // 0   type
        0x00, 0x2A,                                     // 1   locate = 42
        0x00, 0x01,                                     // 3   tracking = 1
        0x00, 0x00, 0x1F, 0x2B, 0x3C, 0x4D,             // 5   ts (6 bytes)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xD2, // 11  order ref = 1234
        'B',                                            // 19  side
        0x00, 0x00, 0x01, 0xF4,                         // 20  shares = 500
        'A',  'A',  'P',  'L',  ' ',  ' ',  ' ',  ' ',  // 24  stock
        0x00, 0x16, 0xE3, 0x60,                         // 32  price = 1500000 = $150.0000
    };
    CHECK(msg.size() == kMsgLen['A']);

    const unsigned char* p = msg.data();
    CHECK(p[off::kType] == 'A');
    CHECK(be16(p + off::kStockLocate) == 42);
    CHECK(timestamp(p) == 0x00001F2B3C4DULL);
    CHECK(tracking(p) == 1); // decoded from the same load as the timestamp
    CHECK(be64(p + off::add::kOrderRef) == 1234);
    CHECK(p[off::add::kSide] == 'B');
    CHECK(be32(p + off::add::kShares) == 500);
    CHECK(alpha(p + off::add::kStock, 8) == "AAPL");
    CHECK(be32(p + off::add::kPrice) == 1500000);
}

// ---------------------------------------------------------------------------
// 'U' Order Replace. 35 bytes, specification section 1.4.5. Two references: the
// old one is retired, the new one carries every subsequent update, and side and
// stock are retained from the original Add.
// ---------------------------------------------------------------------------
static void test_order_replace() {
    const std::vector<unsigned char> msg = {
        'U',  0x00, 0x2A, 0x00, 0x01, 0x00, 0x00, 0x1F, 0x2B, 0x3C,
        0x4E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xD2, // 11  old ref = 1234
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xD3,       // 19  new ref = 1235
        0x00, 0x00, 0x00, 0xC8,                               // 27  shares = 200
        0x00, 0x16, 0xE4, 0xC4,                               // 31  price = 1500356
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

    std::vector<unsigned char> sysev(12, 0);
    sysev[0] = 'S';
    sysev[11] = 'O';
    frame(sysev);

    // Known type at the wrong length: skipped and counted rather than parsed,
    // and it must not desynchronise the frames that follow.
    std::vector<unsigned char> bad(20, 0);
    bad[0] = 'D'; // 'D' should be 19
    frame(bad);

    // Unknown type byte.
    std::vector<unsigned char> unk(10, 0);
    unk[0] = '~';
    frame(unk);

    std::vector<unsigned char> del(19, 0);
    del[0] = 'D';
    frame(del);

    buf.push_back(0);
    buf.push_back(0); // end of session

    FrameReader rd(std::span<const unsigned char>{buf.data(), buf.size()});
    MsgView m;
    int ok = 0;
    for (;;) {
        const FrameStatus st = rd.next(m);
        if (st == FrameStatus::EndOfSession) break;
        if (st == FrameStatus::Truncated) {
            CHECK(false && "unexpected truncation");
            break;
        }
        if (st == FrameStatus::Ok) ++ok;
    }
    CHECK(ok == 2);            // the S and the good D
    CHECK(rd.mismatch() == 1); // the bad D
    CHECK(rd.unknown() == 1);  // the '~'
}

// Every known type's length must round-trip through the table.
static void test_length_table() {
    const char* known = "SRHYLVWKJhAFECXDUPQBINO";
    int n = 0;
    for (const char* c = known; *c; ++c) {
        CHECK(kMsgLen[(unsigned char)*c] != 0);
        ++n;
    }
    CHECK(n == 23);
    CHECK(kMsgLen['Z'] == 0);
    CHECK(kMsgLen['I'] == kMaxMsgLen);
}

// The offset-3 load reads the tracking number and timestamp together, so a
// nonzero tracking number must not appear in the timestamp's high bits.
static void test_timestamp_masking() {
    unsigned char m[12] = {'S', 0, 0, 0xBE, 0xEF, 0x00, 0x1F, 0x2B, 0x3C, 0x4D, 0x5E, 'O'};
    CHECK(timestamp(m) == 0x001F2B3C4D5EULL);
    CHECK(tracking(m) == 0xBEEF);
}

// ---------------------------------------------------------------------------
// The zero-prefixed framing variant.
//
// ex20101224.TEST_ITCH_50, bundled with the RITCH R package, carries the
// 2-byte prefix field for every message and leaves all of them zero, so the
// length must come from the type byte. In the prefixed form the same bytes
// mean end of session, which is why the two forms are distinguished before
// the walk rather than during it.
// ---------------------------------------------------------------------------

// Frames a body with the given 2-byte prefix value.
static void frame_with(std::vector<unsigned char>& buf, std::uint16_t prefix,
                       const std::vector<unsigned char>& body) {
    buf.push_back(static_cast<unsigned char>(prefix >> 8));
    buf.push_back(static_cast<unsigned char>(prefix));
    buf.insert(buf.end(), body.begin(), body.end());
}

static std::vector<unsigned char> body_of(char type) {
    std::vector<unsigned char> b(kMsgLen[static_cast<unsigned char>(type)], 0);
    b[off::kType] = static_cast<unsigned char>(type);
    return b;
}

static void test_zero_prefixed_framing() {
    std::vector<unsigned char> buf;
    frame_with(buf, 0, body_of('S'));
    frame_with(buf, 0, body_of('A'));
    frame_with(buf, 0, body_of('D'));
    // No terminator: this form ends by exhausting the buffer at a boundary.

    CHECK(detect_framing({buf.data(), buf.size()}) == Framing::ZeroPrefixed);

    FrameReader rd({buf.data(), buf.size()}, Framing::ZeroPrefixed);
    MsgView m;
    int ok = 0;
    unsigned char types[3] = {0, 0, 0};
    for (;;) {
        const FrameStatus st = rd.next(m);
        if (st == FrameStatus::EndOfSession) break;
        if (st != FrameStatus::Ok) {
            CHECK(false && "unexpected status in the zero-prefixed form");
            break;
        }
        if (ok < 3) types[ok] = m.type();
        ++ok;
    }
    CHECK(ok == 3);
    CHECK(types[0] == 'S');
    CHECK(types[1] == 'A');
    CHECK(types[2] == 'D');
    CHECK(rd.offset() == buf.size());
    CHECK(rd.unknown() == 0);
    CHECK(rd.mismatch() == 0);
}

// An unknown type byte is recoverable in the prefixed form and terminal in the
// zero-prefixed form, because nothing says how far to skip.
static void test_zero_prefixed_unknown_type_is_terminal() {
    std::vector<unsigned char> buf;
    frame_with(buf, 0, body_of('S'));
    std::vector<unsigned char> unk(10, 0);
    unk[off::kType] = '~';
    frame_with(buf, 0, unk);
    frame_with(buf, 0, body_of('D'));

    FrameReader rd({buf.data(), buf.size()}, Framing::ZeroPrefixed);
    MsgView m;
    CHECK(rd.next(m) == FrameStatus::Ok);
    CHECK(m.type() == 'S');
    CHECK(rd.next(m) == FrameStatus::Truncated);
    CHECK(rd.unknown() == 1);
}

// Detection must not mistake one form for the other. A real session begins
// with a prefix equal to the type-implied length; the RITCH fixture begins
// with a zero prefix and a valid type byte.
// NASDAQ's published sessions carry no zero-length terminator: they end with
// the 'S' end-of-messages event and then the file ends. That is a well-formed
// end, and only a buffer that stops part-way through a message is a
// truncation.
static void test_exhaustion_at_a_boundary_is_a_clean_end() {
    std::vector<unsigned char> buf;
    frame_with(buf, kMsgLen['A'], body_of('A'));
    std::vector<unsigned char> sysev = body_of('S');
    sysev[11] = 'C'; // end of messages
    frame_with(buf, kMsgLen['S'], sysev);
    // No terminator.

    FrameReader rd({buf.data(), buf.size()});
    MsgView m;
    CHECK(rd.next(m) == FrameStatus::Ok);
    CHECK(rd.next(m) == FrameStatus::Ok);
    CHECK(rd.next(m) == FrameStatus::EndOfSession);
    CHECK(rd.offset() == buf.size());

    // A body cut short is still a truncation, so the relaxation above does not
    // hide a genuinely damaged file.
    std::vector<unsigned char> cut = buf;
    cut.pop_back();
    FrameReader rd2({cut.data(), cut.size()});
    CHECK(rd2.next(m) == FrameStatus::Ok);
    CHECK(rd2.next(m) == FrameStatus::Truncated);

    // A dangling single byte is a truncation too.
    std::vector<unsigned char> stub = buf;
    stub.push_back(0);
    FrameReader rd3({stub.data(), stub.size()});
    CHECK(rd3.next(m) == FrameStatus::Ok);
    CHECK(rd3.next(m) == FrameStatus::Ok);
    CHECK(rd3.next(m) == FrameStatus::Truncated);

    // The zero-length terminator is still accepted where it is present.
    std::vector<unsigned char> terminated = buf;
    terminated.push_back(0);
    terminated.push_back(0);
    FrameReader rd4({terminated.data(), terminated.size()});
    CHECK(rd4.next(m) == FrameStatus::Ok);
    CHECK(rd4.next(m) == FrameStatus::Ok);
    CHECK(rd4.next(m) == FrameStatus::EndOfSession);
}

static void test_framing_detection() {
    std::vector<unsigned char> prefixed;
    frame_with(prefixed, kMsgLen['S'], body_of('S'));
    frame_with(prefixed, kMsgLen['R'], body_of('R'));
    frame_with(prefixed, kMsgLen['A'], body_of('A'));
    prefixed.push_back(0);
    prefixed.push_back(0);
    CHECK(detect_framing({prefixed.data(), prefixed.size()}) == Framing::LengthPrefixed);

    std::vector<unsigned char> zeroed;
    frame_with(zeroed, 0, body_of('S'));
    frame_with(zeroed, 0, body_of('R'));
    frame_with(zeroed, 0, body_of('A'));
    CHECK(detect_framing({zeroed.data(), zeroed.size()}) == Framing::ZeroPrefixed);

    // Too short to tell, and garbage, both resolve to the prefixed form: it is
    // the form every session in docs/data.md uses and the only one that can
    // recover from a bad frame.
    const std::vector<unsigned char> tiny = {0x00};
    CHECK(detect_framing({tiny.data(), tiny.size()}) == Framing::LengthPrefixed);
    const std::vector<unsigned char> junk = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(detect_framing({junk.data(), junk.size()}) == Framing::LengthPrefixed);
}

int main() {
    test_timestamp_masking();
    test_add_order();
    test_order_replace();
    test_framing();
    test_zero_prefixed_framing();
    test_zero_prefixed_unknown_type_is_terminal();
    test_exhaustion_at_a_boundary_is_a_clean_end();
    test_framing_detection();
    test_length_table();
    if (failures == 0)
        std::printf("all wire tests passed\n");
    else
        std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
