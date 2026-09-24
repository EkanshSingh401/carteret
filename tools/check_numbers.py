#!/usr/bin/env python3
"""Fail if docs/preregistration.md quotes a number the computation did not produce.

A registration whose numbers are typed in by hand is a registration whose
numbers can drift from the code that made them -- silently, and in the
direction the author prefers. `research/gated.py` writes every figure it
produces to `docs/generated/gated_values.tsv`, formatted exactly as the
registration must quote it. This checks the two agree.

Every quoted figure carries a tag naming its key, placed immediately after the
value:

    | Accuracy bar | **55.16%** <!--gen:accuracy_bar--> |

The check is that the text ending at the tag is the generated value, character
for character. Markdown emphasis around the value is ignored; nothing else is.

Exit codes:
  0  every tagged value matches
  1  a mismatch, an unknown key, or a missing generated file

    usage: tools/check_numbers.py
"""

from __future__ import annotations

import pathlib
import re
import sys

DOC = pathlib.Path("docs/preregistration.md")
GEN = pathlib.Path("docs/generated/gated_values.tsv")
TAG = re.compile(r"<!--gen:([A-Za-z0-9_]+)-->")


def load_generated() -> dict[str, str]:
    if not GEN.exists():
        print(f"{GEN} is missing. Run research/gated.py.", file=sys.stderr)
        raise SystemExit(1)
    out: dict[str, str] = {}
    for line in GEN.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        k, _, v = line.partition("\t")
        out[k] = v
    return out


def matches(text: str, end: int, want: str) -> tuple[bool, str]:
    """Does the rendered text immediately before a tag end with the value?

    Asking whether the text ends with the expected value, rather than trying to
    parse the value out, avoids guessing where a value begins -- which differs
    between "2,203,916", "55.16%" and "not triggered". A boundary check stops
    "17" from satisfying a tag whose value is "7": the character before the
    match must not itself be part of a number.
    """
    head = text[:end].rstrip().rstrip("*").rstrip()
    if not head.endswith(want):
        tail = head[-40:]
        return False, f"...{tail}"
    before = head[: len(head) - len(want)]
    if before and before[-1] in "0123456789.,%":
        return False, f"...{head[-40:]}"
    return True, want


def main() -> int:
    if not DOC.exists():
        print(f"{DOC} is missing", file=sys.stderr)
        return 1
    gen = load_generated()
    text = DOC.read_text()

    bad = 0
    seen: set[str] = set()
    for m in TAG.finditer(text):
        key = m.group(1)
        seen.add(key)
        line_no = text.count("\n", 0, m.start()) + 1
        if key not in gen:
            print(f"{DOC}:{line_no}: unknown key {key!r}; "
                  f"{GEN} has no such value", file=sys.stderr)
            bad += 1
            continue
        want = gen[key]
        ok, got = matches(text, m.start(), want)
        if not ok:
            print(f"{DOC}:{line_no}: {key}: expected the text before the tag to "
                  f"end with {want!r}, found {got!r}", file=sys.stderr)
            bad += 1

    print(f"checked {len(seen)} tagged values in {DOC} against {GEN}")
    unquoted = sorted(set(gen) - seen)
    if unquoted:
        print(f"note: {len(unquoted)} generated values are not quoted "
              f"in the registration: {', '.join(unquoted)}")
    if bad:
        print(f"FAILED: {bad} mismatch(es)", file=sys.stderr)
        return 1
    print("all tagged values match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
