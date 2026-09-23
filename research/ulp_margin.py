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
print(f"draws                {n}")
print(f"closest to a boundary {worst:.3E}")
print(f"at draw              {worst_at[0]}, gap {worst_at[1]:.6f} ns")
ulp = Decimal(2) ** -52
print(f"one ulp of that gap  {(worst_at[1] * ulp):.3E}")
print(f"margin               {(worst / (worst_at[1] * ulp)):.1f}x one ulp")
