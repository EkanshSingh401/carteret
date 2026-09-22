// determinism -- hashes a session's reconstructed event stream and final book.
//
// Correctness layer 5. Prints two digests; --check compares them against
// expected values and exits nonzero on any difference, which is how CI pins
// the synthetic session.
//
//   usage: determinism [--check <event-hex> <book-hex>] <session-file>
//
// A change in either digest is a change in the reconstruction. That is not
// automatically a defect -- a deliberate semantic fix changes it too -- but it
// must be explained in the commit that causes it, or the golden values mean
// nothing.

#include "carteret/determinism.hpp"
#include "carteret/mapped_file.hpp"
#include "carteret/parser.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace carteret;

int main(int argc, char** argv) {
    std::string path;
    std::string want_events;
    std::string want_book;
    bool check = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--check") == 0) {
            if (i + 2 >= argc) {
                std::fprintf(stderr, "--check needs an event hash and a book hash\n");
                return 2;
            }
            check = true;
            want_events = argv[++i];
            want_book = argv[++i];
        } else {
            path = argv[i];
        }
    }
    if (path.empty()) {
        std::fprintf(stderr, "usage: %s [--check <event-hex> <book-hex>] <session-file>\n",
                     argv[0]);
        return 2;
    }

    MappedFile mf(path);
    DeterministicReplay r;
    Parser<DeterministicReplay> parser(r);
    const ParseStats st = parser.run(mf.bytes());

    const std::string events = r.event_hash();
    const std::string book = r.book_hash();

    std::printf("file          %s\n", path.c_str());
    std::printf("messages      %llu\n", (unsigned long long)st.dispatched);
    std::printf("effects       %llu\n", (unsigned long long)r.events.count());
    std::printf("live orders   %zu\n", r.book.live_orders());
    std::printf("event stream  %s\n", events.c_str());
    std::printf("book state    %s\n", book.c_str());

    if (!check) return st.clean() ? 0 : 1;

    int rc = 0;
    if (events != want_events) {
        std::fprintf(stderr, "event stream hash CHANGED\n  expected %s\n  actual   %s\n",
                     want_events.c_str(), events.c_str());
        rc = 1;
    }
    if (book != want_book) {
        std::fprintf(stderr, "book state hash CHANGED\n  expected %s\n  actual   %s\n",
                     want_book.c_str(), book.c_str());
        rc = 1;
    }
    if (rc) {
        std::fprintf(stderr,
                     "\nThe reconstruction changed. If that was intended, explain it in the\n"
                     "commit message and update tests/golden_hashes.txt in the same commit.\n");
    } else {
        std::printf("\ndeterminism: both hashes match\n");
    }
    return rc;
}
