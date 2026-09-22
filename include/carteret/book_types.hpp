// carteret/book_types.hpp -- the vocabulary both book implementations share.
//
// Kept separate so that the reference book and the fast book agree on these by
// construction rather than by two definitions that could drift. The
// differential harness compares values of these types across the two, so a
// divergence in the types themselves would be invisible to it.

#pragma once

#include <cstdint>

namespace carteret {

// Price(4): an unsigned integer with four implied decimal places. Money is
// integral everywhere in C++; see docs/design.md record 004.
using Price = std::uint32_t;

// Order reference number. Day-unique, not reliably increasing (record 012).
using Ref = std::uint64_t;

using Shares = std::uint32_t;

// Side codes, as they appear on the wire.
inline constexpr unsigned char kBuy = 'B';
inline constexpr unsigned char kSell = 'S';

} // namespace carteret
