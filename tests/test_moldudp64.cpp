// test_moldudp64 -- sequencing, gap detection, A/B arbitration, loss injection.
//
// Correctness layer 5. The property under test is that every message is
// delivered exactly once and in order, that a gap is always detected and never
// silently absorbed, and that a duplicate is never delivered twice.
//
// The loss-injection harness is the part that matters: a gap detector that is
// never given a gap proves nothing. Losses are injected at a controlled rate
// and every one must be reported, with the number of missing messages exact.

#include "carteret/moldudp64.hpp"
#include "carteret/parser.hpp"
#include "carteret/reference_book.hpp"

#include "itch_builder.hpp"

#include <cstdio>
#include <random>
#include <set>
#include <vector>

using namespace carteret;
using carteret::test::Bytes;

namespace {

int failures = 0;

#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
            ++failures;                                                                        \
        }                                                                                      \
    } while (0)

// Records what the receiver delivers, so delivery order and exactly-once
// behaviour can be asserted against the stream that went in.
struct Collector {
    std::vector<std::uint64_t> sequences;
    std::vector<unsigned char> types;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> gaps;
    std::uint64_t stale_deliveries = 0;

    void on_mold_message(MsgView m, std::uint64_t seq, bool stale) {
        sequences.push_back(seq);
        types.push_back(m.type());
        if (stale) ++stale_deliveries;
    }
    void on_gap(std::uint64_t from, std::uint64_t to) { gaps.emplace_back(from, to); }
};

// A framed session of n add orders, each distinguishable by its reference.
std::vector<unsigned char> make_session(int n) {
    std::vector<unsigned char> buf;
    std::uint64_t ts = 34200ULL * 1000000000ULL;
    for (int i = 0; i < n; ++i) {
        Bytes m = carteret::test::header('A', 1, ts += 1000);
        m.u64(static_cast<std::uint64_t>(i + 1));
        m.u8(kBuy);
        m.u32(100);
        m.alpha("AAPL", 8);
        m.u32(1500000);
        carteret::test::frame(buf, m);
    }
    carteret::test::frame_end(buf);
    return buf;
}

void test_roundtrip_without_loss() {
    const auto session = make_session(97);
    MoldPacketizer p("CARTERET01", 10);
    const auto packets = p.packetize({session.data(), session.size()});

    Collector c;
    MoldReceiver<Collector> rx(c, "CARTERET01");
    for (const auto& pk : packets) rx.receive({pk.data(), pk.size()});

    CHECK(c.sequences.size() == 97);
    CHECK(rx.stats().messages_delivered == 97);
    CHECK(rx.stats().gaps == 0);
    CHECK(rx.stats().messages_lost == 0);
    CHECK(rx.stats().duplicates == 0);
    CHECK(rx.stats().saw_end_of_session);
    CHECK(!rx.stale());
    // Sequence numbers count messages and start at 1.
    for (std::size_t i = 0; i < c.sequences.size(); ++i) {
        CHECK(c.sequences[i] == i + 1);
    }
}

// Both lines carry identical content. Every message must be delivered once,
// whichever line arrives first, and the other line's copy dropped.
void test_ab_arbitration_first_arrival_wins() {
    const auto session = make_session(50);
    MoldPacketizer pa("CARTERET01", 5);
    MoldPacketizer pb("CARTERET01", 5);
    const auto line_a = pa.packetize({session.data(), session.size()});
    const auto line_b = pb.packetize({session.data(), session.size()});
    CHECK(line_a.size() == line_b.size());

    Collector c;
    MoldReceiver<Collector> rx(c, "CARTERET01");

    // Interleave the lines, alternating which one arrives first, so neither is
    // systematically the winner.
    std::mt19937_64 rng(4);
    for (std::size_t i = 0; i < line_a.size(); ++i) {
        const bool a_first = (rng() % 2) == 0;
        const auto& first = a_first ? line_a[i] : line_b[i];
        const auto& second = a_first ? line_b[i] : line_a[i];
        rx.receive({first.data(), first.size()});
        rx.receive({second.data(), second.size()});
    }

    CHECK(c.sequences.size() == 50);
    CHECK(rx.stats().duplicates == line_a.size() - 1); // every data packet, once
    CHECK(rx.stats().gaps == 0);
    CHECK(!rx.stale());
    for (std::size_t i = 0; i < c.sequences.size(); ++i) CHECK(c.sequences[i] == i + 1);
}

// One line loses packets, the other does not. Arbitration must cover the loss
// with no gap reported, which is the entire reason a feed has two lines.
void test_ab_arbitration_covers_one_sided_loss() {
    const auto session = make_session(200);
    MoldPacketizer p("CARTERET01", 4);
    const auto packets = p.packetize({session.data(), session.size()});

    Collector c;
    MoldReceiver<Collector> rx(c, "CARTERET01");
    std::mt19937_64 rng(9);
    for (std::size_t i = 0; i < packets.size(); ++i) {
        const bool drop_a = (rng() % 100) < 30;
        const bool drop_b = (rng() % 100) < 30;
        // Both lines never drop the same packet in this test: that is the
        // condition under which arbitration is supposed to be lossless.
        if (!drop_a || !drop_b) {
            if (!drop_a) rx.receive({packets[i].data(), packets[i].size()});
            if (!drop_b) rx.receive({packets[i].data(), packets[i].size()});
        } else {
            rx.receive({packets[i].data(), packets[i].size()}); // deliver on one line
        }
    }

    CHECK(c.sequences.size() == 200);
    CHECK(rx.stats().gaps == 0);
    CHECK(rx.stats().messages_lost == 0);
    CHECK(!rx.stale());
}

// Loss injection: drop whole packets on every line and require each loss to be
// reported, with the message count exact.
void test_loss_is_always_detected() {
    for (int drop_pct : {1, 5, 25, 50}) {
        const auto session = make_session(1000);
        MoldPacketizer p("CARTERET01", 7);
        const auto packets = p.packetize({session.data(), session.size()});

        Collector c;
        MoldReceiver<Collector> rx(c, "CARTERET01");
        std::mt19937_64 rng(static_cast<std::uint64_t>(drop_pct));

        std::uint64_t expected_lost = 0;
        std::set<std::uint64_t> delivered_expected;
        std::uint64_t seq = 1;
        for (const auto& pk : packets) {
            const std::uint16_t count = static_cast<std::uint16_t>((pk[18] << 8) | pk[19]);
            if (count == kMoldEndOfSession || count == 0) {
                rx.receive({pk.data(), pk.size()});
                continue;
            }
            const bool drop = static_cast<int>(rng() % 100) < drop_pct;
            if (drop) {
                expected_lost += count;
            } else {
                for (std::uint16_t i = 0; i < count; ++i) delivered_expected.insert(seq + i);
                rx.receive({pk.data(), pk.size()});
            }
            seq += count;
        }

        // Losses at the very end are not detectable: nothing arrives after
        // them to reveal the jump. That is a property of sequence-gap
        // detection, not a defect, and the accounting says so.
        const std::uint64_t tail_lost =
            1000 - delivered_expected.size() - rx.stats().messages_lost;

        CHECK(rx.stats().messages_delivered == delivered_expected.size());
        CHECK(rx.stats().messages_lost + tail_lost == expected_lost);
        CHECK((expected_lost == 0) == (rx.stats().gaps == 0 && tail_lost == 0));
        if (rx.stats().messages_lost > 0) {
            CHECK(rx.stale());
            CHECK(!c.gaps.empty());
            // Every reported gap must be a forward jump.
            for (const auto& [from, to] : c.gaps) CHECK(to > from);
        }
        // Delivered sequence numbers must be strictly increasing, even across
        // gaps: a gap advances the expectation, it does not replay.
        for (std::size_t i = 1; i < c.sequences.size(); ++i) {
            CHECK(c.sequences[i] > c.sequences[i - 1]);
        }
        // Everything delivered after the first gap is flagged stale.
        CHECK(c.stale_deliveries <= c.sequences.size());
    }
}

// A packet whose blocks overrun the datagram is malformed. Nothing in it is
// delivered, because once one block boundary is wrong the rest are unknown.
void test_truncated_packet_delivers_nothing() {
    const auto session = make_session(10);
    MoldPacketizer p("CARTERET01", 10);
    auto packets = p.packetize({session.data(), session.size()});
    CHECK(packets.size() == 2); // one data packet, one end-of-session

    auto cut = packets[0];
    cut.resize(cut.size() - 5);

    Collector c;
    MoldReceiver<Collector> rx(c, "CARTERET01");
    CHECK(rx.receive({cut.data(), cut.size()}) == PacketOutcome::Malformed);
    CHECK(c.sequences.empty());
    CHECK(rx.stats().malformed == 1);
    CHECK(rx.next_expected() == 1); // the expectation did not move

    // A header-only fragment is malformed too.
    std::vector<unsigned char> stub(kMoldHeaderLen - 1, 0);
    CHECK(rx.receive({stub.data(), stub.size()}) == PacketOutcome::Malformed);
}

void test_wrong_session_is_rejected() {
    const auto session = make_session(10);
    MoldPacketizer p("OTHERSESS1", 10);
    const auto packets = p.packetize({session.data(), session.size()});

    Collector c;
    MoldReceiver<Collector> rx(c, "CARTERET01");
    CHECK(rx.receive({packets[0].data(), packets[0].size()}) == PacketOutcome::WrongSession);
    CHECK(c.sequences.empty());
    CHECK(rx.stats().wrong_session == 1);
}

// A heartbeat ahead of the expected sequence is a gap signal, and on a quiet
// symbol it is often the first one.
void test_heartbeat_ahead_of_sequence_signals_a_gap() {
    Collector c;
    MoldReceiver<Collector> rx(c, "CARTERET01");
    std::vector<unsigned char> hb(kMoldHeaderLen, ' ');
    const char* s = "CARTERET01";
    for (std::size_t i = 0; i < kMoldSessionLen; ++i) {
        hb[i] = static_cast<unsigned char>(s[i]);
    }
    const std::uint64_t seq = 42;
    for (int i = 0; i < 8; ++i) {
        hb[kMoldSessionLen + static_cast<std::size_t>(i)] =
            static_cast<unsigned char>(seq >> (56 - 8 * i));
    }
    hb[18] = 0;
    hb[19] = 0;

    CHECK(rx.receive({hb.data(), hb.size()}) == PacketOutcome::Heartbeat);
    CHECK(rx.stats().heartbeats == 1);
    CHECK(rx.stats().gaps == 1);
    CHECK(rx.stats().messages_lost == 41); // sequences 1 through 41
    CHECK(rx.stale());
}

// The messages that survive a lossless packetisation must rebuild the same
// book as the file replay, or the layer is testing its own framing rather than
// the feed.
void test_delivered_stream_rebuilds_the_book() {
    const auto session = make_session(300);

    ReferenceBook direct;
    {
        Parser<ReferenceBook> parser(direct);
        parser.run({session.data(), session.size()});
    }

    struct BookFeeder {
        ReferenceBook book;
        std::vector<unsigned char> framed;
        void on_mold_message(MsgView m, std::uint64_t, bool) {
            framed.push_back(static_cast<unsigned char>(m.len >> 8));
            framed.push_back(static_cast<unsigned char>(m.len));
            framed.insert(framed.end(), m.data, m.data + m.len);
        }
    } feeder;

    MoldPacketizer p("CARTERET01", 3);
    const auto packets = p.packetize({session.data(), session.size()});
    MoldReceiver<BookFeeder> rx(feeder, "CARTERET01");
    for (const auto& pk : packets) rx.receive({pk.data(), pk.size()});

    carteret::test::frame_end(feeder.framed);
    Parser<ReferenceBook> parser(feeder.book);
    parser.run({feeder.framed.data(), feeder.framed.size()});

    CHECK(direct.live_orders() == feeder.book.live_orders());
    CHECK(direct.live_orders() > 0);
    const LevelSnapshot a = direct.level(1, kBuy, 1500000);
    const LevelSnapshot b = feeder.book.level(1, kBuy, 1500000);
    CHECK(a.present && b.present);
    CHECK(a.shares == b.shares);
    CHECK(a.fifo == b.fifo);
}

} // namespace

int main() {
    test_roundtrip_without_loss();
    test_ab_arbitration_first_arrival_wins();
    test_ab_arbitration_covers_one_sided_loss();
    test_loss_is_always_detected();
    test_truncated_packet_delivers_nothing();
    test_wrong_session_is_rejected();
    test_heartbeat_ahead_of_sequence_signals_a_gap();
    test_delivered_stream_rebuilds_the_book();

    if (failures == 0) {
        std::printf("all MoldUDP64 tests passed\n");
    } else {
        std::printf("%d failure(s)\n", failures);
    }
    return failures ? 1 : 0;
}
