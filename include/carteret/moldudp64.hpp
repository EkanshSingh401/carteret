// carteret/moldudp64.hpp -- MoldUDP64 framing, sequencing and line arbitration.
//
// The session files this project replays are already complete and in order. A
// live feed is neither: it arrives as UDP datagrams that can be lost,
// duplicated or reordered, on two lines that carry the same content. What a
// reconstruction does about that is a correctness question the file replay
// cannot ask, so the layer is built and driven against a packetised synthetic
// stream (docs/correctness.md, layer 5).
//
// Packet layout, from the MoldUDP64 specification:
//
//   offset 0   10 bytes  session, alphanumeric, left justified
//   offset 10   8 bytes  sequence number of the FIRST message in the packet
//   offset 18   2 bytes  message count; 0 is a heartbeat, 0xFFFF ends the session
//   offset 20   ...      message count blocks, each a 2-byte length then payload
//
// All integers are big-endian. Sequence numbers count messages, not packets,
// which is what makes the size of a gap knowable in messages.
//
// What a gap means here. A lost packet's contents are unknown by definition:
// the receiver learns only how many messages went missing, not which symbols
// they touched. There is therefore no sound way to mark a subset of symbols
// stale, and this implementation marks the whole book stale and says so. The
// recovery path for a real gap is a GLIMPSE snapshot followed by a replay of
// the remainder, which is **out of scope** -- this layer stops at detection.
// Pretending to recover would be worse than not recovering, because the book
// would then be wrong without being flagged.

#pragma once

#include "wire.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace carteret {

inline constexpr std::size_t kMoldSessionLen = 10;
inline constexpr std::size_t kMoldHeaderLen = 20;
inline constexpr std::uint16_t kMoldEndOfSession = 0xFFFF;

struct MoldHeader {
    std::string_view session;
    std::uint64_t sequence = 0;
    std::uint16_t message_count = 0;

    [[nodiscard]] bool heartbeat() const noexcept { return message_count == 0; }
    [[nodiscard]] bool end_of_session() const noexcept {
        return message_count == kMoldEndOfSession;
    }
};

// Builds MoldUDP64 packets from a BinaryFILE-framed stream. Used to turn the
// synthetic session into something the receiver can be driven with, and to
// inject loss against.
class MoldPacketizer {
public:
    MoldPacketizer(std::string_view session, std::uint16_t max_messages_per_packet = 10)
        : max_msgs_(max_messages_per_packet ? max_messages_per_packet : 1) {
        session_.fill(' ');
        for (std::size_t i = 0; i < session.size() && i < kMoldSessionLen; ++i) {
            session_[i] = static_cast<unsigned char>(session[i]);
        }
    }

    // Splits a framed stream into packets. Each packet carries the sequence
    // number of its first message, so a receiver that drops a packet learns
    // the gap size in messages from the next packet's number.
    [[nodiscard]] std::vector<std::vector<unsigned char>>
    packetize(std::span<const unsigned char> framed) {
        std::vector<std::vector<unsigned char>> out;
        FrameReader rd(framed);
        MsgView m;
        std::vector<unsigned char> packet;
        std::uint16_t in_packet = 0;

        const auto flush = [&] {
            if (in_packet == 0) return;
            packet[18] = static_cast<unsigned char>(in_packet >> 8);
            packet[19] = static_cast<unsigned char>(in_packet);
            out.push_back(packet);
            next_seq_ += in_packet;
            in_packet = 0;
            packet.clear();
        };

        for (;;) {
            const FrameStatus st = rd.next(m);
            if (st != FrameStatus::Ok && st != FrameStatus::LengthMismatch &&
                st != FrameStatus::UnknownType) {
                break;
            }
            if (st != FrameStatus::Ok) continue;

            if (in_packet == 0) packet = header_bytes(next_seq_);
            packet.push_back(static_cast<unsigned char>(m.len >> 8));
            packet.push_back(static_cast<unsigned char>(m.len));
            packet.insert(packet.end(), m.data, m.data + m.len);
            ++in_packet;
            if (in_packet == max_msgs_) flush();
        }
        flush();

        // End-of-session packet: message count 0xFFFF, no message blocks.
        std::vector<unsigned char> eos = header_bytes(next_seq_);
        eos[18] = 0xFF;
        eos[19] = 0xFF;
        out.push_back(eos);
        return out;
    }

private:
    [[nodiscard]] std::vector<unsigned char> header_bytes(std::uint64_t seq) const {
        std::vector<unsigned char> h(kMoldHeaderLen, 0);
        std::memcpy(h.data(), session_.data(), kMoldSessionLen);
        for (int i = 0; i < 8; ++i) {
            h[kMoldSessionLen + static_cast<std::size_t>(i)] =
                static_cast<unsigned char>(seq >> (56 - 8 * i));
        }
        return h;
    }

    std::array<unsigned char, kMoldSessionLen> session_{};
    std::uint16_t max_msgs_;
    std::uint64_t next_seq_ = 1; // MoldUDP64 sequence numbers start at 1
};

enum class PacketOutcome : unsigned char {
    Delivered,    // in order; its messages were handed to the handler
    Duplicate,    // already seen on the other line, or a retransmission
    Gap,          // sequence jumped forward; messages were lost
    Heartbeat,    // message count 0
    EndOfSession, // message count 0xFFFF
    Malformed,    // too short, or its blocks do not fit the datagram
    WrongSession, // a different session id
};

struct MoldStats {
    std::uint64_t packets = 0;
    std::uint64_t delivered = 0;
    std::uint64_t duplicates = 0;
    std::uint64_t heartbeats = 0;
    std::uint64_t malformed = 0;
    std::uint64_t wrong_session = 0;
    std::uint64_t gaps = 0;
    std::uint64_t messages_lost = 0;
    std::uint64_t messages_delivered = 0;
    bool saw_end_of_session = false;
    bool stale = false; // set on the first gap and never cleared
};

// Receives packets from one or both lines and delivers each message exactly
// once, in order.
//
// Arbitration is first arrival wins: a packet whose sequence range has already
// been delivered is dropped whichever line it came from. That is the whole of
// A/B arbitration for a feed where both lines carry identical content, and it
// needs no knowledge of which line a packet came from -- which is what makes
// it robust to one line running ahead of the other by an arbitrary amount.
template<class Handler>
class MoldReceiver {
public:
    explicit MoldReceiver(Handler& h, std::string_view session = {}) : h_(h) {
        expect_session_.fill(' ');
        has_session_filter_ = !session.empty();
        for (std::size_t i = 0; i < session.size() && i < kMoldSessionLen; ++i) {
            expect_session_[i] = static_cast<unsigned char>(session[i]);
        }
    }

    PacketOutcome receive(std::span<const unsigned char> packet) {
        ++stats_.packets;
        if (packet.size() < kMoldHeaderLen) {
            ++stats_.malformed;
            return PacketOutcome::Malformed;
        }
        if (has_session_filter_ &&
            std::memcmp(packet.data(), expect_session_.data(), kMoldSessionLen) != 0) {
            ++stats_.wrong_session;
            return PacketOutcome::WrongSession;
        }

        std::uint64_t seq = 0;
        for (int i = 0; i < 8; ++i) {
            seq = (seq << 8) | packet[kMoldSessionLen + static_cast<std::size_t>(i)];
        }
        const std::uint16_t count = static_cast<std::uint16_t>((packet[18] << 8) | packet[19]);

        if (count == kMoldEndOfSession) {
            stats_.saw_end_of_session = true;
            return PacketOutcome::EndOfSession;
        }
        if (count == 0) {
            ++stats_.heartbeats;
            // A heartbeat still carries a sequence number, and a heartbeat
            // ahead of the expected sequence is as much a gap signal as a data
            // packet would be -- often the first one, on a quiet symbol.
            if (seq > next_expected_) note_gap(seq);
            return PacketOutcome::Heartbeat;
        }

        // Already delivered: the other line got here first, or this is a
        // retransmission. Dropped without being parsed.
        if (seq + count <= next_expected_) {
            ++stats_.duplicates;
            return PacketOutcome::Duplicate;
        }

        PacketOutcome outcome = PacketOutcome::Delivered;
        if (seq > next_expected_) {
            note_gap(seq);
            outcome = PacketOutcome::Gap;
        }

        // Walk the message blocks. A packet that overruns its own datagram is
        // malformed and nothing in it is delivered, because the boundary
        // between one block and the next is no longer trustworthy.
        std::size_t pos = kMoldHeaderLen;
        std::vector<std::pair<std::size_t, std::uint16_t>> blocks;
        blocks.reserve(count);
        for (std::uint16_t i = 0; i < count; ++i) {
            if (pos + 2 > packet.size()) {
                ++stats_.malformed;
                return PacketOutcome::Malformed;
            }
            const std::uint16_t len =
                static_cast<std::uint16_t>((packet[pos] << 8) | packet[pos + 1]);
            pos += 2;
            if (pos + len > packet.size()) {
                ++stats_.malformed;
                return PacketOutcome::Malformed;
            }
            blocks.emplace_back(pos, len);
            pos += len;
        }

        // Deliver only the messages at or beyond the expected sequence, so a
        // packet that partially overlaps what the other line already delivered
        // does not replay them.
        for (std::uint16_t i = 0; i < count; ++i) {
            const std::uint64_t msg_seq = seq + i;
            if (msg_seq < next_expected_) continue;
            MsgView m;
            m.data = packet.data() + blocks[i].first;
            m.len = blocks[i].second;
            h_.on_mold_message(m, msg_seq, stats_.stale);
            ++stats_.messages_delivered;
            next_expected_ = msg_seq + 1;
        }
        ++stats_.delivered;
        return outcome;
    }

    [[nodiscard]] const MoldStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::uint64_t next_expected() const noexcept { return next_expected_; }

    // True once a gap has been seen. Never cleared: the messages are gone, and
    // nothing short of a snapshot can make the book correct again. See the
    // header comment on GLIMPSE.
    [[nodiscard]] bool stale() const noexcept { return stats_.stale; }

private:
    void note_gap(std::uint64_t seq) {
        ++stats_.gaps;
        stats_.messages_lost += seq - next_expected_;
        stats_.stale = true;
        if constexpr (requires { h_.on_gap(next_expected_, seq); }) {
            h_.on_gap(next_expected_, seq);
        }
        next_expected_ = seq;
    }

    Handler& h_;
    std::array<unsigned char, kMoldSessionLen> expect_session_{};
    bool has_session_filter_ = false;
    std::uint64_t next_expected_ = 1;
    MoldStats stats_;
};

} // namespace carteret
