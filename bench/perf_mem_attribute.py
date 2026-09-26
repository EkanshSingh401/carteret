#!/usr/bin/env python3
"""Attributes perf mem load samples to the book's structures.

The third of record 015's three attributions. The per-message counters in
attribution.hpp say how many lines each message filled from DRAM; they cannot
say which structure those lines belonged to. perf mem can: on Zen 3 it samples
retired ops through IBS, and each sampled load carries its data address, the
level that served it and its latency. This resolves each address against the
region map bench_book writes (--region-map), which lists the exact range of
every structure the book allocated, taken from the allocations themselves.

The symbol table is split by offset into the occupancy bitmaps and the rest of
each side (the window pointer, the overflow map's header and the window
origin), because record 015 names the bitmaps as a structure of their own.

    perf mem record -o mem.data -- taskset -c 8 \\
        build/bench/bench_book --mode structures --region-map map.txt SESSION
    perf script -i mem.data -F comm,addr,data_src,weight,ip \\
        | bench/perf_mem_attribute.py map.txt

IBS samples every Nth op regardless of type, so a structure's share of load
samples estimates its share of loads, and its share of summed weight estimates
its share of load latency. Samples are counted only for loads by bench_book at
user addresses; kernel samples, which IBS takes too, are dropped.
"""

import bisect
import re
import sys
from collections import defaultdict

LEVELS = [
    ("RAM hit", "dram"),
    ("core, same node Any cache hit", "l3"),
    ("L3 hit", "l3"),
    ("L2 hit", "l2"),
    ("L1 hit", "l1"),
]

SAMPLE = re.compile(
    r"^\s*(?P<comm>\S+)\s+(?P<addr>[0-9a-f]+)\s+(?P<src>[0-9a-f]+)\s+"
    r"\|OP (?P<op>[^|]*)\|LVL (?P<lvl>[^|]*)\|.*?\|BLK\s+[^|]*?\s+(?P<weight>\d+)\s+"
    r"(?P<ip>[0-9a-f]+)\s*$"
)


def load_map(path):
    regions, layout = [], {}
    with open(path) as f:
        for line in f:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "layout":
                it = iter(parts[1:])
                layout = {k: int(v) for k, v in zip(it, it)}
            elif parts[0] == "region":
                regions.append((int(parts[2], 16), int(parts[3], 16), parts[1]))
    regions.sort()
    return regions, layout


def classify(addr, starts, regions, layout):
    i = bisect.bisect_right(starts, addr) - 1
    if i < 0 or addr >= regions[i][1]:
        return "other (stack, heap, libraries)"
    start, _, label = regions[i]
    if label == "symbols" and layout:
        off = (addr - start) % layout["symbol_bytes"] % layout["side_bytes"]
        lo = layout["bitmap_off"]
        if lo <= off < lo + layout["bitmap_bytes"]:
            return "bitmaps"
        return "symbols (side headers)"
    return label


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: perf script ... | perf_mem_attribute.py <region-map> [perf-script.txt]")
    regions, layout = load_map(sys.argv[1])
    starts = [r[0] for r in regions]
    src = open(sys.argv[2]) if len(sys.argv) > 2 else sys.stdin

    count = defaultdict(lambda: defaultdict(int))
    weight = defaultdict(int)
    unparsed = 0
    for line in src:
        if "OP LOAD" not in line:
            continue
        m = SAMPLE.match(line)
        if not m:
            unparsed += 1
            continue
        if m["comm"] != "bench_book":
            continue
        addr = int(m["addr"], 16)
        if addr == 0 or addr >= 0xFFFF800000000000:
            continue
        lvl = next((short for text, short in LEVELS if m["lvl"].startswith(text)), "other")
        label = classify(addr, starts, regions, layout)
        count[label][lvl] += 1
        count[label]["all"] += 1
        weight[label] += int(m["weight"])

    total = sum(c["all"] for c in count.values())
    total_dram = sum(c["dram"] for c in count.values())
    total_w = sum(weight.values())
    if total == 0:
        sys.exit("no user-mode load samples from bench_book")
    print(f"{total} load samples from bench_book at user addresses"
          + (f" ({unparsed} lines not parsed)" if unparsed else ""))
    print(f"{'structure':32} {'loads':>8} {'share':>7} {'dram':>7} {'dram shr':>8} "
          f"{'l3':>7} {'l2':>7} {'l1':>7} {'mean lat':>8} {'lat shr':>7}")
    for label in sorted(count, key=lambda k: -weight[k]):
        c = count[label]
        print(f"{label:32} {c['all']:8d} {100 * c['all'] / total:6.2f}% "
              f"{c['dram']:7d} {100 * c['dram'] / total_dram if total_dram else 0:7.2f}% "
              f"{c['l3']:7d} {c['l2']:7d} {c['l1']:7d} "
              f"{weight[label] / c['all']:8.1f} {100 * weight[label] / total_w:6.2f}%")
    print(f"{'total':32} {total:8d} {100.0:6.2f}% {total_dram:7d} {100.0:7.2f}%")
    print("mean lat is IBS load latency in cycles; lat shr is the share of summed latency")


if __name__ == "__main__":
    main()
