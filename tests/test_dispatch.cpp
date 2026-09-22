// test_dispatch -- the parser's framing loop and typed dispatch.
//
// Covers what the per-type fixtures cannot: that every type with a nonzero
// entry in the length table reaches a handler overload and nothing else does,
// that rejected frames never reach a typed overload and do not desynchronise
// what follows, and that a handler may implement nothing at all.
//
// The coverage test is what keeps kMsgLen and the dispatch switch from
// drifting apart. A type added to the table without a case in the switch fails
// here rather than being silently dropped from every census and every book.

#include "carteret/messages.hpp"
#include "carteret/parser.hpp"
#include "carteret/spec.hpp"

#include <array>
#include <cstdio>
#include <vector>

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

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

// Counts what the parser hands it, per type, and records the frames the reader
// rejected.
struct CountingHandler {
    std::array<std::uint64_t, 256> seen{};
    std::uint64_t unknown = 0;
    std::uint64_t mismatch = 0;

    void on(SystemEvent v) { ++seen[v.type()]; }
    void on(StockDirectory v) { ++seen[v.type()]; }
    void on(StockTradingAction v) { ++seen[v.type()]; }
    void on(RegSHO v) { ++seen[v.type()]; }
    void on(MarketParticipant v) { ++seen[v.type()]; }
    void on(MwcbDeclineLevel v) { ++seen[v.type()]; }
    void on(MwcbStatus v) { ++seen[v.type()]; }
    void on(IpoQuotingPeriod v) { ++seen[v.type()]; }
    void on(LuldAuctionCollar v) { ++seen[v.type()]; }
    void on(OperationalHalt v) { ++seen[v.type()]; }
    void on(AddOrder v) { ++seen[v.type()]; }
    void on(AddOrderMpid v) { ++seen[v.type()]; }
    void on(OrderExecuted v) { ++seen[v.type()]; }
    void on(OrderExecutedPrice v) { ++seen[v.type()]; }
    void on(OrderCancel v) { ++seen[v.type()]; }
    void on(OrderDelete v) { ++seen[v.type()]; }
    void on(OrderReplace v) { ++seen[v.type()]; }
    void on(Trade v) { ++seen[v.type()]; }
    void on(CrossTrade v) { ++seen[v.type()]; }
    void on(BrokenTrade v) { ++seen[v.type()]; }
    void on(Noii v) { ++seen[v.type()]; }
    void on(Rpii v) { ++seen[v.type()]; }
    void on(DirectListingCapRaise v) { ++seen[v.type()]; }

    void on_unknown_type(MsgView) { ++unknown; }
    void on_length_mismatch(MsgView) { ++mismatch; }
};

void frame_into(std::vector<unsigned char>& buf, const unsigned char* body, std::size_t n) {
    buf.push_back(static_cast<unsigned char>(n >> 8));
    buf.push_back(static_cast<unsigned char>(n));
    buf.insert(buf.end(), body, body + n);
}

// Every type with a nonzero length-table entry must reach a handler overload,
// and nothing else may. This is what keeps kMsgLen and the dispatch switch
// from drifting apart: a type added to the table without a case in the switch
// fails here.
void test_dispatch_covers_every_type() {
    std::vector<unsigned char> buf;
    int expected = 0;
    for (std::size_t t = 0; t < 256; ++t) {
        const std::uint8_t len = kMsgLen[t];
        if (len == 0) continue;
        std::vector<unsigned char> body(len, 0);
        body[off::kType] = static_cast<unsigned char>(t);
        frame_into(buf, body.data(), body.size());
        ++expected;
    }
    buf.push_back(0);
    buf.push_back(0);

    CountingHandler h;
    Parser<CountingHandler> parser(h);
    const ParseStats st = parser.run({buf.data(), buf.size()});

    CHECK(expected == 23);
    CHECK(st.dispatched == static_cast<std::uint64_t>(expected));
    CHECK(st.end == ParseEnd::EndOfSession);
    CHECK(st.clean());

    int dispatched_types = 0;
    for (std::size_t t = 0; t < 256; ++t) {
        if (kMsgLen[t]) {
            CHECK(h.seen[t] == 1);
            ++dispatched_types;
        } else {
            CHECK(h.seen[t] == 0);
        }
    }
    CHECK(dispatched_types == 23);
}

// Unknown types and length mismatches are counted and never handed to a typed
// overload, and neither desynchronises the frames that follow.
void test_dispatch_rejects_bad_frames() {
    std::vector<unsigned char> buf;

    std::vector<unsigned char> good(kMsgLen['D'], 0);
    good[off::kType] = 'D';
    frame_into(buf, good.data(), good.size());

    std::vector<unsigned char> wrong_len(kMsgLen['D'] + 1, 0);
    wrong_len[off::kType] = 'D';
    frame_into(buf, wrong_len.data(), wrong_len.size());

    std::vector<unsigned char> unknown(16, 0);
    unknown[off::kType] = '~';
    frame_into(buf, unknown.data(), unknown.size());

    std::vector<unsigned char> good2(kMsgLen['A'], 0);
    good2[off::kType] = 'A';
    frame_into(buf, good2.data(), good2.size());

    buf.push_back(0);
    buf.push_back(0);

    CountingHandler h;
    Parser<CountingHandler> parser(h);
    const ParseStats st = parser.run({buf.data(), buf.size()});

    CHECK(st.dispatched == 2);
    CHECK(st.unknown == 1);
    CHECK(st.mismatch == 1);
    CHECK(h.unknown == 1);
    CHECK(h.mismatch == 1);
    CHECK(h.seen['D'] == 1);
    CHECK(h.seen['A'] == 1); // the frame after the bad ones still decoded
    CHECK(!st.clean());
    CHECK(st.end == ParseEnd::EndOfSession);
}

// A buffer with no end-of-session marker is reported as truncated rather than
// as a clean end.
void test_truncation_is_reported() {
    std::vector<unsigned char> buf;
    std::vector<unsigned char> good(kMsgLen['D'], 0);
    good[off::kType] = 'D';
    frame_into(buf, good.data(), good.size());
    buf.pop_back(); // cut the last body byte

    CountingHandler h;
    Parser<CountingHandler> parser(h);
    const ParseStats st = parser.run({buf.data(), buf.size()});
    CHECK(st.dispatched == 0);
    CHECK(st.end == ParseEnd::Truncated);
    CHECK(!st.clean());
}

// A handler that implements nothing must still drive the parser, because
// dispatch to an absent overload compiles to nothing.
struct EmptyHandler {};

void test_handler_may_implement_nothing() {
    std::vector<unsigned char> buf;
    std::vector<unsigned char> body(kMsgLen['A'], 0);
    body[off::kType] = 'A';
    frame_into(buf, body.data(), body.size());
    buf.push_back(0);
    buf.push_back(0);

    EmptyHandler h;
    Parser<EmptyHandler> parser(h);
    const ParseStats st = parser.run({buf.data(), buf.size()});
    CHECK(st.dispatched == 1);
    CHECK(st.clean());
}

} // namespace

int main() {
    test_dispatch_covers_every_type();
    test_dispatch_rejects_bad_frames();
    test_truncation_is_reported();
    test_handler_may_implement_nothing();

    if (failures == 0) {
        std::printf("all dispatch tests passed\n");
    } else {
        std::printf("%d failure(s)\n", failures);
    }
    return failures ? 1 : 0;
}
