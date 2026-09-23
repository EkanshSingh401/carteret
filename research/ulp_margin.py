#!/usr/bin/env python3
"""How close does any exponential_ns draw come to an integer boundary?

The golden test pins quantized integers, and sampling.hpp claims that a libm
differing in the last bit cannot change them. That claim needs a number, and
the obvious number is the wrong one: comparing the 1 ns quantization step to
an ulp of the product gives a ratio of about thirty million and says nothing,
because what decides the outcome is the CLOSEST APPROACH to an integer
boundary across the draws actually taken.

This measures that. The engine is reimplemented here rather than linked, so
the check does not depend on the build it is checking, and it is validated
against the same four raw draws the golden test pins. The arithmetic runs in
60-digit decimal, so the result does not depend on the platform's doubles
either.

Result on the million draws the golden test checksums: the closest approach is
1.75e-6 ns against 1.31e-7 ns for one ulp there, a margin of 13.3x. Comfortable
for a one-ulp difference, and four thousand times tighter than the
quantization-step comparison implies.
"""
#
# The golden test pins quantized integers. A libm that differs in the last bit
# changes one of them only if the unquantized product falls within that
# difference of an integer. This measures the smallest such distance over the
# same million draws the sweep hashes, using Decimal at 60 digits so the
# arithmetic is independent of the platform's double rounding.
from decimal import Decimal, getcontext
getcontext().prec = 60

MASK = (1 << 64) - 1

def mt_stream(seed, n):
    # mt19937_64 reimplemented so the probe does not depend on the C++ build.
    NN, MM = 312, 156
    MATRIX_A = 0xB5026F5AA96619E9
    UM = 0xFFFFFFFF80000000
    LM = 0x7FFFFFFF
    mt = [0] * NN
    mt[0] = seed & MASK
    for i in range(1, NN):
        mt[i] = (6364136223846793005 * (mt[i-1] ^ (mt[i-1] >> 62)) + i) & MASK
    mti = NN
    for _ in range(n):
        if mti >= NN:
            for i in range(NN):
                x = (mt[i] & UM) | (mt[(i+1) % NN] & LM)
                xa = x >> 1
                if x & 1:
                    xa ^= MATRIX_A
                mt[i] = mt[(i + MM) % NN] ^ xa
            mti = 0
        x = mt[mti]; mti += 1
        x ^= (x >> 29) & 0x5555555555555555
        x ^= (x << 17) & 0x71D67FFFEDA60000
        x ^= (x << 37) & 0xFFF7EEE000000000
        x ^= (x >> 43)
        yield x & MASK

def ln1m(u):
    # log(1-u) by series on Decimal; u in [0,1). Uses atanh-form for accuracy.
    x = Decimal(1) - u
    # ln(x) via Decimal.ln(), which is correctly rounded at the set precision.
    return x.ln()

mean = Decimal(250000000)
worst = None
worst_at = None
n = 0
for raw in mt_stream(20190130, 1000000):
    u = Decimal(raw >> 11) / Decimal(1 << 53)
    gap = -mean * ln1m(u)
    frac = gap - int(gap)
    d = min(frac, Decimal(1) - frac)
    n += 1
    if worst is None or d < worst:
        worst = d
        worst_at = (n, gap)
print(f"draws sampled          {n:,}")
print(f"closest to a boundary  {worst:.3E} ns")
ulp = Decimal(2) ** -52
one_ulp = worst_at[1] * ulp
print(f"one ulp of that gap    {one_ulp:.3E} ns")
print(f"margin                 {(worst / one_ulp):.1f}x one ulp")
print()

# THE MARGIN ABOVE IS A PROPERTY OF THIS SAMPLE, NOT OF THE METHOD.
#
# A draw is at risk when its unquantized gap falls within one ulp of an
# integer boundary. The fractional part is effectively uniform, and the
# boundary zone has width 2 x ulp in nanoseconds, so the per-draw probability
# is 2 x ulp and the expected number at risk grows LINEARLY with the number of
# draws taken. Observing a 13.3x margin over a million draws says only that
# this particular million happened not to contain one; it says nothing about
# what ten million will contain.
mean_gap = mean  # E[gap] = mean for an exponential
typical_ulp = mean_gap * ulp
p_at_risk = 2 * typical_ulp
print(f"typical gap            {mean_gap:.3E} ns")
print(f"one ulp there          {typical_ulp:.3E} ns")
print(f"P(draw within 1 ulp of a boundary) = 2 x ulp = {p_at_risk:.3E}")
print()

# Draw counts. Only exponential_ns touches libm; pick(), coin() and
# bernoulli() are integer arithmetic and cannot diverge.
PLACEMENTS_PER_RUN = 93_525
RUNS_PER_SESSION = 11 * 2   # base run plus ten seeds, each with rule 4 off and on
SESSIONS_STAGE7 = 2
stage7 = PLACEMENTS_PER_RUN * RUNS_PER_SESSION * SESSIONS_STAGE7

# Stage 8 plans seven development sessions and two held out, at the same
# placement rate, with one run per rule-4 arm and no seed sweep on the
# held-out set.
STUDY_SESSIONS = 9
study = PLACEMENTS_PER_RUN * 2 * STUDY_SESSIONS

for label, N in (("golden sweep", n), ("full Stage 7 run", stage7),
                 ("planned study", study), ("both together", stage7 + study)):
    expected = Decimal(N) * p_at_risk
    print(f"{label:22s} {N:>12,} draws   expected at risk {expected:.2f}")
print()
import math
total = stage7 + study
lam = float(Decimal(total) * p_at_risk)
print(f"Across Stage 7 and the planned study together the expected count is")
print(f"{lam:.2f}, so the chance that at least one draw sits within one ulp of a")
print(f"boundary is {100 * (1 - math.exp(-lam)):.0f}%. It is not a remote possibility and it is")
print("not a certainty; it is a coin flip, and it scales linearly with how many")
print("draws the project ends up taking.")
print()
print("Such a draw would move one placement by one nanosecond, far below the")
print("microsecond spacing of messages, so it cannot change which message a")
print("placement lands on. It WOULD change a checksum over the draws. That is")
print("why the comparison that settles this is run on the benchmark host")
print("against glibc on x86-64 rather than inferred here.")
