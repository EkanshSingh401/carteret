#!/usr/bin/env python3
"""Fail if a document quotes a number its source does not contain.

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

README.md is checked too, and may also carry a second kind of tag naming a
committed file rather than a generated key:

    | Batch, mean per book message | **167.78 ns** <!--src:docs/benchmarks.md--> |

The number immediately before a `src:` tag (a unit such as "ns" between them
is allowed) must occur in that file as a number in its own right, not as part
of a longer one. Thousands separators and the Unicode minus are normalised on
both sides, so "1,140" in the README matches "1140" in a table. This does not
prove the file is right; it proves the README says what the file says, and
names the file a reader should check.

Exit codes:
  0  every tagged value matches
  1  a mismatch, an unknown key, a missing generated file, or a src: file
     that is absent or not tracked by git

    usage: tools/check_numbers.py [repository-root]
"""

from __future__ import annotations

import os
import pathlib
import re
import subprocess
import sys

DOCS = (pathlib.Path("docs/preregistration.md"), pathlib.Path("README.md"))
GEN = pathlib.Path("docs/generated/gated_values.tsv")
TAG = re.compile(r"<!--gen:([A-Za-z0-9_]+)-->")
SRC = re.compile(r"<!--src:([^>]+?)-->")
# A number at the end of the text before a src: tag, optionally followed by a
# short unit. The sign may be ASCII or U+2212.
TRAILING = re.compile(r"([+\-\u2212]?\d[\d,]*(?:\.\d+)?%?)\s*(?:[A-Za-z]{1,3})?\s*$")


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


def norm(s: str) -> str:
    return s.replace(",", "").replace("\u2212", "-")


def in_file(value: str, body: str) -> bool:
    """Does the value occur in body as a whole number, after normalising?"""
    want = norm(value)
    hay = norm(body)
    pat = re.compile(r"(?<![\d.])" + re.escape(want) + r"(?![\d]|\.\d)")
    if pat.search(hay):
        return True
    # "+31%" in the README is "31%" in a sentence; a leading "+" is presentation.
    if want.startswith("+"):
        return in_file(want[1:], body)
    return False


def tracked(path: str) -> bool:
    """Is the file committed, not merely present in the working tree?"""
    r = subprocess.run(["git", "ls-files", "--error-unmatch", "--", path],
                       capture_output=True)
    return r.returncode == 0


def check_src(doc: pathlib.Path, text: str) -> tuple[int, int]:
    bad = 0
    n = 0
    cache: dict[str, str] = {}
    for m in SRC.finditer(text):
        n += 1
        path = m.group(1).strip()
        line_no = text.count("\n", 0, m.start()) + 1
        src = pathlib.Path(path)
        if path not in cache:
            if not src.is_file() or not tracked(path):
                print(f"{doc}:{line_no}: src {path!r} is not a committed file",
                      file=sys.stderr)
                bad += 1
                continue
            cache[path] = src.read_text()
        head = text[: m.start()].rstrip().rstrip("*").rstrip()
        v = TRAILING.search(head)
        if not v:
            print(f"{doc}:{line_no}: no number before the src tag: "
                  f"...{head[-40:]!r}", file=sys.stderr)
            bad += 1
            continue
        if not in_file(v.group(1), cache[path]):
            print(f"{doc}:{line_no}: {v.group(1)!r} does not occur in {path}",
                  file=sys.stderr)
            bad += 1
    return n, bad


def main() -> int:
    if len(sys.argv) > 1:
        os.chdir(sys.argv[1])
    gen = load_generated()
    bad = 0
    seen: set[str] = set()
    for doc in DOCS:
        if not doc.exists():
            print(f"{doc} is missing", file=sys.stderr)
            return 1
        text = doc.read_text()
        tagged = 0
        for m in TAG.finditer(text):
            key = m.group(1)
            seen.add(key)
            tagged += 1
            line_no = text.count("\n", 0, m.start()) + 1
            if key not in gen:
                print(f"{doc}:{line_no}: unknown key {key!r}; "
                      f"{GEN} has no such value", file=sys.stderr)
                bad += 1
                continue
            want = gen[key]
            ok, got = matches(text, m.start(), want)
            if not ok:
                print(f"{doc}:{line_no}: {key}: expected the text before the tag "
                      f"to end with {want!r}, found {got!r}", file=sys.stderr)
                bad += 1
        print(f"checked {tagged} generated values in {doc} against {GEN}")
        n, b = check_src(doc, text)
        bad += b
        if n:
            print(f"checked {n} sourced values in {doc} against the files they name")

    unquoted = sorted(set(gen) - seen)
    if unquoted:
        print(f"note: {len(unquoted)} generated values are not quoted: "
              f"{', '.join(unquoted)}")
    if bad:
        print(f"FAILED: {bad} mismatch(es)", file=sys.stderr)
        return 1
    print("all tagged values match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
