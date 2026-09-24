#!/usr/bin/env python3
"""Fail if the registration carries an unregistered placeholder.

A "(to be filled)" left in `docs/preregistration.md` is not a formatting
blemish. It means a constant the study depends on was never registered, and a
component that reads it cannot produce a confirmatory result. That happened:
section 8's signal threshold and minimum-fills rule were both left unfilled
and the gap was found only by reading the held-out harness before running it.

This gate makes the next one impossible to miss. Every placeholder must be
listed in the allowlist table of `docs/heldout-harness-amendment.md`, together
with the component it demotes to exploratory. A placeholder that is not listed
fails CI; a listed entry with no matching placeholder also fails, so the
allowlist cannot outlive what it excuses.

    usage: tools/check_placeholders.py
"""

from __future__ import annotations

import pathlib
import re
import sys

DOC = pathlib.Path("docs/preregistration.md")
ALLOW = pathlib.Path("docs/heldout-harness-amendment.md")

# The forms that mean "not decided yet". TBD is included because it is the
# other way the same gap gets written.
PLACEHOLDER = re.compile(r"\*?\(to be filled\)\*?|(?<![A-Za-z])TBD(?![A-Za-z])",
                         re.IGNORECASE)

# Rows of the allowlist table: | `phrase` | component |
ROW = re.compile(r"^\|\s*`([^`]+)`\s*\|\s*([^|]+?)\s*\|\s*$", re.MULTILINE)


def allowlist() -> dict[str, str]:
    if not ALLOW.exists():
        return {}
    text = ALLOW.read_text()
    start = text.find("<!-- placeholder-allowlist -->")
    if start < 0:
        return {}
    end = text.find("<!-- /placeholder-allowlist -->", start)
    block = text[start:end if end > 0 else len(text)]
    return {m.group(1): m.group(2).strip() for m in ROW.finditer(block)}


def main() -> int:
    if not DOC.exists():
        print(f"{DOC} is missing", file=sys.stderr)
        return 1
    text = DOC.read_text()
    allowed = allowlist()

    # Match by SPAN, not by line. Section 8 carries both placeholders on one
    # line, and a per-line check would mark the line covered by whichever
    # allowlist entry it happened to find first, leaving the other entry
    # looking stale. Every placeholder OCCURRENCE must fall inside the span of
    # some allowlisted phrase.
    allowed_spans: list[tuple[int, int, str]] = []
    for phrase in allowed:
        start = 0
        while True:
            i = text.find(phrase, start)
            if i < 0:
                break
            allowed_spans.append((i, i + len(phrase), phrase))
            start = i + 1

    bad = 0
    matched: set[str] = set()
    found = list(PLACEHOLDER.finditer(text))
    for m in found:
        line_no = text.count("\n", 0, m.start()) + 1
        cover = next((sp for sp in allowed_spans
                      if sp[0] <= m.start() and m.end() <= sp[1]), None)
        if cover is None:
            line = text.splitlines()[line_no - 1].strip()
            print(f"{DOC}:{line_no}: unregistered placeholder, and no allowlist "
                  f"entry covers it:\n    {line}", file=sys.stderr)
            bad += 1
        else:
            matched.add(cover[2])
            print(f"{DOC}:{line_no}: allowed -- demotes: {allowed[cover[2]]}")

    print(f"placeholders found {len(found)}, allowed {len(matched)}, "
          f"allowlist entries {len(allowed)}")
    if bad:
        print(f"FAILED: {bad} problem(s)", file=sys.stderr)
        return 1
    print("every placeholder is registered in the allowlist")
    return 0


if __name__ == "__main__":
    sys.exit(main())
