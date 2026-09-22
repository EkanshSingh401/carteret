#!/usr/bin/env bash
# Writes a seed corpus for the framing fuzz target.
#
# One input per message type in each framing form, plus a few whole sessions.
# The first byte of every input selects the framing form, matching
# tests/fuzz_frame.cpp.
#
#   usage: tools/gen_fuzz_corpus.sh <corpus-dir>
set -euo pipefail

DIR="${1:?usage: gen_fuzz_corpus.sh <corpus-dir>}"
mkdir -p "$DIR"

python3 - "$DIR" <<'PY'
import os, struct, sys

out = sys.argv[1]
LEN = {'S':12,'R':39,'H':25,'Y':20,'L':26,'V':35,'W':12,'K':28,'J':35,'h':21,
       'A':36,'F':40,'E':31,'C':36,'X':23,'D':19,'U':35,'P':44,'Q':40,'B':19,
       'I':50,'N':20,'O':48}

def body(t, fill=0x41):
    b = bytearray([fill]) * LEN[t]
    b[0] = ord(t)
    b[1:3] = struct.pack('>H', 7)      # stock locate
    b[3:5] = struct.pack('>H', 0xBEEF) # tracking number, nonzero
    b[5:11] = (34200 * 10**9).to_bytes(6, 'big')
    return bytes(b)

def prefixed(bodies):
    out = b''
    for b in bodies:
        out += struct.pack('>H', len(b)) + b
    return out + b'\x00\x00'

def zero_prefixed(bodies):
    return b''.join(b'\x00\x00' + b for b in bodies)

def write(name, framing_byte, payload):
    with open(os.path.join(out, name), 'wb') as f:
        f.write(bytes([framing_byte]) + payload)

# One input per type, in each form. Framing byte: even -> length-prefixed,
# odd -> zero-prefixed, matching the target.
for t in LEN:
    tag = 'lower_' + t if t.islower() else t
    write(f'one_{tag}_prefixed', 0x00, prefixed([body(t)]))
    write(f'one_{tag}_zeroed',   0x01, zero_prefixed([body(t)]))

# Whole sessions covering every type in one buffer.
every = [body(t) for t in LEN]
write('session_prefixed', 0x00, prefixed(every))
write('session_zeroed',   0x01, zero_prefixed(every))

# A plausible order-flow sequence, which is what a book replay actually sees.
flow = [body('S'), body('R'), body('A'), body('F'), body('E'), body('C'),
        body('X'), body('U'), body('D'), body('P'), body('Q'), body('B'),
        body('S')]
write('session_flow_prefixed', 0x00, prefixed(flow))
write('session_flow_zeroed',   0x01, zero_prefixed(flow))

# Degenerate inputs the fuzzer would otherwise have to find on its own.
write('empty_prefixed',    0x00, b'\x00\x00')
write('truncated_prefix',  0x00, b'\x00')
write('truncated_body',    0x00, struct.pack('>H', LEN['A']) + body('A')[:10])
write('unknown_type',      0x00, prefixed([b'~' + bytes(15)]))
write('length_mismatch',   0x00, struct.pack('>H', LEN['D'] + 1) + body('D') + b'\x00' + b'\x00\x00')
write('max_length_prefix', 0x00, b'\xff\xff' + bytes(16))

print(f'wrote {len(os.listdir(out))} seed inputs')
PY
