// venue_profile -- decide which venue a session is from by its CONTENT.
//
// The archive's directory layout is not reliable. `05302019.NASDAQ_ITCH50.gz`
// is filed under `Nasdaq PSX ITCH/` beside genuine `*.PSX_ITCH_50.gz` files,
// and `S101819-v50.txt.gz` follows neither naming convention. A session's
// venue decides its fee model and therefore whether it may be pooled with
// another (docs/design.md record 037), so it has to be established from the
// bytes rather than from where the bytes were found.
//
// The discriminator is the AUCTION. NASDAQ runs an opening and a closing
// cross and disseminates Net Order Imbalance Indicators throughout; BX and
// PSX run neither. So:
//
//   NASDAQ   millions of 'I' (NOII), thousands of 'Q' (Cross Trade) including
//            cross types 'O' (opening) and 'C' (closing)
//   BX, PSX  no 'I' at all, and no 'O' or 'C' cross
//
// That is a difference of kind, not of degree, which is what makes it usable
// as a verdict rather than as evidence.
//
//   usage: venue_profile <session-file> [...]

#include "carteret/mapped_file.hpp"
#include "carteret/parser.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

using namespace carteret;

namespace {

struct Profile {
    std::uint64_t messages = 0;
    std::array<std::uint64_t, 256> by_type{};
    std::array<std::uint64_t, 256> cross_type{}; // 'O' 'C' 'H' 'I'
    std::uint64_t noii = 0;                      // count of 'I'
    std::uint64_t noii_paired = 0;               // summed paired shares
    std::uint64_t cross_shares = 0;

    template<class V>
    void count(V v) {
        ++messages;
        ++by_type[v.type()];
    }

    void on(SystemEvent v) { count(v); }
    void on(StockDirectory v) { count(v); }
    void on(StockTradingAction v) { count(v); }
    void on(RegSHO v) { count(v); }
    void on(MarketParticipant v) { count(v); }
    void on(MwcbDeclineLevel v) { count(v); }
    void on(MwcbStatus v) { count(v); }
    void on(IpoQuotingPeriod v) { count(v); }
    void on(LuldAuctionCollar v) { count(v); }
    void on(OperationalHalt v) { count(v); }
    void on(AddOrder v) { count(v); }
    void on(AddOrderMpid v) { count(v); }
    void on(OrderExecuted v) { count(v); }
    void on(OrderExecutedPrice v) { count(v); }
    void on(OrderCancel v) { count(v); }
    void on(OrderDelete v) { count(v); }
    void on(OrderReplace v) { count(v); }
    void on(Trade v) { count(v); }
    void on(BrokenTrade v) { count(v); }
    void on(Rpii v) { count(v); }
    void on(DirectListingCapRaise v) { count(v); }

    void on(CrossTrade v) {
        count(v);
        ++cross_type[v.cross_type()];
        cross_shares += v.shares();
    }
    void on(Noii v) {
        count(v);
        ++noii;
        noii_paired += v.paired_shares();
    }
};

const char* verdict(const Profile& p) {
    const std::uint64_t opening = p.cross_type[static_cast<unsigned char>('O')];
    const std::uint64_t closing = p.cross_type[static_cast<unsigned char>('C')];
    if (p.noii > 0 && opening > 0 && closing > 0) return "NASDAQ";
    if (p.noii == 0 && opening == 0 && closing == 0) return "NOT NASDAQ (no auction)";
    return "AMBIGUOUS -- inspect before use";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <session-file> [...]\n", argv[0]);
        return 2;
    }
    std::printf("%-28s %14s %12s %10s %10s %12s  %s\n", "session", "messages", "NOII 'I'",
                "cross 'O'", "cross 'C'", "paired sh", "verdict");
    int bad = 0;
    for (int i = 1; i < argc; ++i) {
        MappedFile mf(argv[i]);
        Profile p;
        Parser<Profile> parser(p);
        parser.run(mf.bytes());
        const char* base = std::strrchr(argv[i], '/');
        const std::string name = base ? base + 1 : argv[i];
        const char* v = verdict(p);
        std::printf("%-28s %14llu %12llu %10llu %10llu %12llu  %s\n", name.c_str(),
                    (unsigned long long)p.messages, (unsigned long long)p.noii,
                    (unsigned long long)p.cross_type[static_cast<unsigned char>('O')],
                    (unsigned long long)p.cross_type[static_cast<unsigned char>('C')],
                    (unsigned long long)p.noii_paired, v);
        if (std::strncmp(v, "AMBIG", 5) == 0) bad = 1;
    }
    return bad;
}
