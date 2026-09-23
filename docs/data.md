# Data

NASDAQ publishes free sample TotalView-ITCH 5.0 sessions at
<https://emi.nasdaq.com/ITCH/>. The files are large and carry redistribution
restrictions, so `data/` is gitignored and every session is fetched with
`tools/fetch_data.sh` rather than committed. No market data appears anywhere in
this repository, including in `docs/figures/`.

Listing below taken from the directory index on **2026-09-22**. Sizes are
compressed bytes as reported by the index; unpacked sizes run roughly 1.5x to
3.5x larger. The index is not stable — see *Provenance* — so this table records
what was available on that date and is re-checked before each stage that
consumes new sessions.

## The 2017 BX session is not available

`20170130.BX_ITCH_50`, which earlier versions of this project's tooling used as
the default session, **returns HTTP 404**, both at `/ITCH/` and at
`/ITCH/Nasdaq BX ITCH/`. The BX directory's earliest session with a retrievable
file is `20181228`. Entries for 2018 BX sessions exist as `.md5sum` files with
no corresponding `.gz`.

The nearest analogue on the same calendar date is **`20190130.BX_ITCH_50`**
(1.10 GB compressed), which is the session this project uses for the Stage 1
census gate and the Stage 2 and Stage 3 reference replays.

## Layout of the archive

Sessions live under a per-venue subdirectory, not at `/ITCH/` directly:

```
/ITCH/Nasdaq ITCH/            NASDAQ
/ITCH/Nasdaq BX ITCH/         BX
/ITCH/Nasdaq PSX ITCH/        PSX
/ITCH/GIS/                    assorted additional sessions
/ITCH/NOII Beta Files/        empty as of 2026-09-22
/ITCH/Stock_Locate_Codes/     per-session locate code tables
```

## NASDAQ

Maker-taker. The venue used by the pre-registered study
(`docs/design.md` record 022).

| Session | File | Compressed | Study assignment |
|---|---|---:|---|
| 2019-01-30 | `Nasdaq ITCH/01302019.NASDAQ_ITCH50.gz` | 4.76 GB | development |
| 2019-03-27 | `Nasdaq ITCH/03272019.NASDAQ_ITCH50.gz` | 5.51 GB | development |
| 2019-05-30 | `Nasdaq PSX ITCH/05302019.NASDAQ_ITCH50.gz` | 4.25 GB | development |
| 2019-07-30 | `Nasdaq ITCH/07302019.NASDAQ_ITCH50.gz` | 3.66 GB | development |
| 2019-08-30 | `Nasdaq ITCH/08302019.NASDAQ_ITCH50.gz` | 4.08 GB | development |
| 2019-10-18 | `Nasdaq ITCH/S101819-v50.txt.gz` | 3.95 GB | development |
| 2019-10-30 | `Nasdaq ITCH/10302019.NASDAQ_ITCH50.gz` | 3.87 GB | **HELD OUT — do not fetch** |
| 2019-12-30 | `Nasdaq ITCH/12302019.NASDAQ_ITCH50.gz` | 3.52 GB | development (spent on Stage 3) |
| 2020-01-30 | `Nasdaq ITCH/01302020.NASDAQ_ITCH50.gz` | 5.60 GB | **HELD OUT — do not fetch** |

### The split, and its one flaw

Seven development sessions, two held out. Recorded **before any feature code
was written**, which is the only property that makes it meaningful. The two
held-out sessions have not been downloaded and must not be until
`research/heldout.lock` names a commit containing the completed
`docs/preregistration.md`.

The flaw, stated rather than buried: **2019-12-30 sits chronologically after
2019-10-30**, so the development set is not strictly earlier than the held-out
set. It was fetched for the Stage 3 correctness gate — which needs a NASDAQ
session, because BX contains no auction messages at all — before the study
split existed, and downloading it is what disqualified it from being held out.
Choosing the two latest sessions instead would have left only 2020-01-30
untouched, and a single held-out session makes session-clustered inference on
the held-out side impossible.

For a signal measured over seconds and book events this interleave is a minor
concern; for a slower signal it would not be acceptable. It is a limitation of
this study, not a property of the method.

The 2019-05-30 NASDAQ session is filed under the PSX directory; the matching
`.md5sum` sits in the NASDAQ directory with no `.gz` beside it. The file is
named `NASDAQ_ITCH50` and is treated as a NASDAQ session. Its checksum cannot
be verified, for the reason in *Checksums are listed but not served* below.

The `GIS/Nov 18, Dec 18, Jan 19/` sessions (2018-12-13, 2018-12-14,
2018-12-31) are in the 2017-2020 window and are **unassigned**. They are held
in reserve for the case where the Stage 8 power analysis shows the seven
development sessions cannot resolve the minimum detectable effect; adding them
to development is one of the remedies that section names.

### Outside the 2017-2020 window

Present in the archive and **not used**, because the cents-indexed price axis
(`docs/design.md` record 005), the fee schedule, and the message-type inventory
are all pinned to the 2017-2020 period:

`itch50_05_15.gz`, `itch50_05_18.gz`, `S061226-v50.txt.gz`,
`S071321-v50.txt.gz`, `S081321-v50.txt.gz`, `S112825-v50.txt.gz`,
`S120825-v50.txt.gz`, `S120925-v50.txt.gz`, `S121025-v50.txt.gz`,
`S121125-v50.txt.gz`, `S121225-v50.txt.gz`, `S010303-v2.zip` (ITCH 2.0, a
different protocol), `tvagg.gz`, `Nasdaq ITCH/NOII/S050922-v50-NOII.txt.gz`.

Sessions from 2022 onward do contain the `O` Direct Listing With Capital Raise
message and the 2022-2023 `I` field values, which no 2017-2020 session can
reach. They are a possible source of coverage for those decode paths and are
not otherwise in scope.

## BX

Taker-maker. Used for engineering and correctness work only; never pooled with
NASDAQ in a cost-inclusive result (`docs/design.md` record 022). A BX session
carries roughly a fifth of a NASDAQ session's messages, which makes the
edit-run loop short enough to iterate on.

| Session | File | Compressed |
|---|---|---:|
| 2018-12-28 | `Nasdaq BX ITCH/20181228.BX_ITCH_50.gz` | 1.66 GB |
| **2019-01-30** | `Nasdaq BX ITCH/20190130.BX_ITCH_50.gz` | **1.10 GB** |
| 2019-03-27 | `Nasdaq BX ITCH/20190327.BX_ITCH_50.gz` | 1.21 GB |
| 2019-05-30 | `Nasdaq BX ITCH/20190530.BX_ITCH_50.gz` | 0.50 GB |
| 2019-07-30 | `Nasdaq BX ITCH/20190730.BX_ITCH_50.gz` | 0.39 GB |
| 2019-08-30 | `Nasdaq BX ITCH/20190830.BX_ITCH_50.gz` | 0.45 GB |
| 2019-10-30 | `Nasdaq BX ITCH/20191030.BX_ITCH_50.gz` | 0.40 GB |
| 2019-12-30 | `Nasdaq BX ITCH/20191230.BX_ITCH_50.gz` | 0.39 GB |
| 2020-01-30 | `Nasdaq BX ITCH/20200130.BX_ITCH_50.gz` | 0.72 GB |
| 2020-01-27 | `GIS/BX Jan 2020/S012720-v50-bx.txt.gz` | 0.65 GB |
| 2020-01-28 | `GIS/BX Jan 2020/S012820-v50-bx.txt.gz` | 0.48 GB |
| 2020-01-31 | `GIS/BX Jan 2020/S013120-v50-bx.txt.gz` | 0.87 GB |
| 2020-03-02 | `Nasdaq BX ITCH/March 20/S030220-v50-bx.txt.gz` | 1.50 GB |

2019-01-30 is the project's primary correctness session. Measured after
fetching, on 2026-09-22:

| | |
|---|---|
| Unpacked | 2,422,694,511 bytes |
| Messages | 82,841,542 |
| Book-affecting (`A F E C X D U`) | 74,182,680 (89.5%) |
| Framing | length-prefixed; ends on System Event `'C'`, no zero-length prefix |
| Trailing bytes | 0 |
| SHA-256 (unpacked) | `d670c9dd0e2391a4007fa407668bfaaa9ded346f0804bb5d7b2a6c381bdcadd3` |
| First / last timestamp | 03:06:49 / 19:05:00 |
| Types present | `S R H Y L V A F E C X D U P B N` (16 of 23) |
| Types absent | `I J K Q W h O` |
| `N` (RPII) | 8,301,264 — 10.0% of the session |
| Fast-book overflow rate (256-tick sliding window) | 2.37% |
| Window recenters / levels moved | 3,206,627 / 9,318,071 |

Per-type counts agree exactly with `RITCH::count_messages()`; see
`docs/correctness.md`. The absent types mean this session establishes nothing
about the `I`, `J`, `K`, `Q`, `W`, `h` or `O` layouts, whose only coverage is
the byte fixtures. BX runs no opening or closing cross, which accounts for `I`
and `Q`.

### 2019-12-30, the first NASDAQ session

Fetched and verified on 2026-09-22 by every check in the section below.

| | |
|---|---|
| Compressed | 3,524,013,057 bytes, matching the advertised length exactly |
| Unpacked | 8,251,407,909 bytes |
| Messages | 268,744,780 |
| Book-affecting (`A F E C X D U`) | 263,241,937 (98.0%) |
| Framing | length-prefixed; ends on System Event `'C'`, no zero-length prefix |
| Trailing bytes | 0 |
| Unknown type / length mismatch | 0 / 0 |
| SHA-256 (unpacked) | `5d81c2e14a0f748b29c674b6a342796932702034b4dd341e39e9a9ec5bac610f` |
| First / last timestamp | 03:04:32 / 20:05:00 |
| Types present | `S R H Y L V K J A F E C X D U P Q I` (18 of 23) |
| Types absent | `N W h O B` |

**This session is what covers the message types BX could not.** BX runs no
opening or closing cross, so `I` (NOII) and `Q` (Cross Trade) were exercised
only by byte fixtures until now; here there are **4,024,315** `I` messages and
**17,836** `Q`. `J` (LULD Auction Collar, 34) and `K` (IPO Quoting Period, 3)
appear for the first time as well, and `V` (MWCB Decline Level) appears once,
which is the message whose Price(8) field is item 8 in the README's wire
notes.

It also settles a question left open in `docs/design.md` record 034. `N`
(RPII) is **10.0% of the BX session and entirely absent here**. The two venues
disagree about that message type completely, which is why a claim about it
made on one venue's data says nothing about the other.

`W` (MWCB Breach), `h` (Operational Halt) and `O` (Retail Price Improvement
Indicator) remain uncovered by any session, and `B` (Broken Trade) appears
only on BX, three times.

Three message types are therefore still known from byte fixtures alone, and
this document does not claim otherwise.

## PSX

Not used. Listed for completeness, since a PSX file sits in the same tree and
is easy to fetch by accident. PSX has its own fee schedule and is no more
poolable with NASDAQ than BX is.

`20181228`, `20190130`, `20190327`, `20190530`, `20190730`, `20190830`,
`20191030`, `20191230`, `20200130`, all as `Nasdaq PSX ITCH/*.PSX_ITCH_50.gz`.

## Other

- `GIS/Nov 18, Dec 18, Jan 19/` — NASDAQ sessions `S121318-v50.txt.gz`
  (2018-12-13, 5.24 GB), `S121418-v50.txt.gz` (2018-12-14, 5.05 GB),
  `S123118-v50.txt.gz` (2018-12-31, 4.93 GB). In the 2017-2020 window and
  available as additional NASDAQ sessions if the Stage 8 power analysis needs
  them.
- `GIS/NOII 2019/12062019.NOII.gz` — NOII-only extract, 2019-12-06, 54 MB. Not
  a full session.
- `GIS/Aug 5-9 2019/` and `NOII Beta Files/` — empty as of 2026-09-22.

## Withdrawn sessions

These appear in the index as `.md5sum` entries with no corresponding data file.
They are listed so that a future re-check can tell "withdrawn" from "never
existed".

- NASDAQ: `01302018`, `03292018`, `05302018`, `07302018`, `08302018`,
  `10302018`, `12282018`, `05302019` (data file misfiled under PSX).
- BX: `20180130`, `20180329`, `20180530`, `20180730`, `20180830`, `20181030`.
- PSX: `20180130`, `20180329`, `20180530`, `20180730`, `20180830`, `20181030`.

## Verifying a session

The archive serves no usable checksum (below), so every session is verified
locally and its digest recorded here. Four checks, in order, because each is
only meaningful once the previous one holds:

1. **Transfer length** equals the server's advertised `content-length`.
   `curl` exiting zero does not establish this; see below.
2. **`gzip -t` passes with no output.** Appended bytes are reported
   inconsistently — macOS gzip exits 2 with "trailing garbage ignored", GNU
   gzip exits 0 with the same warning — so both a nonzero exit and any output
   are treated as failure.
3. **The last message is System Event `'C'`, End of Messages.** This is the
   only check that can detect a truncated session, because without a
   zero-length terminator a truncated file is a well-formed prefix of a valid
   one. `census` exits nonzero if it fails. See `docs/design.md` record 032.
4. **SHA-256 recorded here**, computed twice: by `shasum` in
   `tools/fetch_data.sh` and by the in-tree implementation in `census
   --sha256`. Two independent implementations agreeing on the same bytes.

| Session | SHA-256 (unpacked) | Verified |
|---|---|---|
| `20190130.BX_ITCH_50` | `d670c9dd0e2391a4007fa407668bfaaa9ded346f0804bb5d7b2a6c381bdcadd3` | 2026-09-22 |
| `12302019.NASDAQ_ITCH50` | `5d81c2e14a0f748b29c674b6a342796932702034b4dd341e39e9a9ec5bac610f` | 2026-09-22 |

The compressed files are recorded too, because the length and `gzip -t` checks
are made against those bytes and the digest is what ties a later re-fetch to
the same archive entry.

| Compressed file | Advertised bytes | SHA-256 (.gz) |
|---|---:|---|
| `12302019.NASDAQ_ITCH50.gz` | 3,524,013,057 | `ef03df46a27e6bda4dead017f84c2e3979df7211f02c7868b51d53fceb99c689` |

## Checksums are listed but not served

The index lists a `.md5sum` beside almost every session. **Every one of those
URLs returns HTTP 404**, checked on 2026-09-22 across NASDAQ, BX and both
present and withdrawn sessions. The entries appear in the directory listing
with plausible sizes (67-72 bytes) and cannot be retrieved.

Consequences, stated rather than worked around:

- `tools/fetch_data.sh` attempts the checksum, reports
  `integrity unverified` when it is unavailable, and continues. It does not
  silently skip the check and it does not treat a missing checksum as a pass.
- No result in this repository can claim a checksum-verified input. What it
  can claim is the determinism hash of its own reconstruction
  (`docs/correctness.md`, layer 5), which establishes that two replays of the
  same local bytes agree, not that those bytes are the ones NASDAQ published.
- This is re-checked whenever a session is fetched. If the checksums become
  retrievable, the verification results are recorded in the tables above.

## Fetching a multi-gigabyte session, and three attempts to get it right

The archive drops connections mid-transfer. Fetching
`12302019.NASDAQ_ITCH50.gz` (3,524,013,057 bytes) failed with a connection
reset at 29%, and again with a stall at 38%. Surviving that is necessary, and
the obvious mechanism does not work here.

**Measured on 2026-09-22:**

| Request | Response |
|---|---|
| `HEAD` | 200, `content-length: 3524013057`, `accept-ranges: bytes` |
| `HEAD` with `Range` | **416**, `content-range: bytes */0` |
| `GET` with `Range: bytes=0-0` | 206, `content-range: bytes 0-0/3524013057` |
| `GET` with a mid-file `Range` | 206, correct `content-range` |

### Two failed approaches, both of which produced a file that looked finished

**First attempt: `curl --continue-at -` unconditionally.** On the retry after a
slow-transfer timeout, curl printed `100.0%` and **exited zero with the file
back at exactly the byte offset it had started from** — 29% of the session.
Everything fetched after that offset was discarded.

**Second attempt: probe for range support, then continue.** The probe used
`GET` and correctly saw 206, so continuation was enabled. It worked, reached
71%, and then a retry after a slow-transfer timeout came back with **the whole
body rather than the requested range**, which curl appended to the partial
file. The result was **4,038,899,612 bytes — 115% of the advertised length and
still growing** — with valid gzip at the front and garbage from the restart
offset onward. `gunzip -t` would have caught it; so would the length check;
but only after an hour of bandwidth.

### What the script does now

No partial continuation at all. Each attempt fetches the whole file to
`data/.<name>.partial`, and the file is moved into place only once its length
matches the advertised `content-length`. Up to four attempts, then it gives up
and removes the partial, so a failed fetch never leaves something a later run
could mistake for a good file. The gzip stream is tested before unpacking,
because `gunzip` on a truncated stream yields a plausible prefix of a session
rather than failing at the start, and a short session would otherwise surface
much later as a wrong message count with no obvious cause.

The lesson worth keeping: **`curl` exiting zero is not evidence that a file is
whole**, and neither is a progress bar reaching 100%. Only the length is, and
only then the checksum — which this archive does not serve.

## Provenance

The archive is a live directory listing, not a versioned dataset. Files have
been added and removed over the project's lifetime — the 2017 BX session is one
casualty and the 2018 sessions are another. Consequences:

1. Every result names the session it came from by file name and date.
2. This table records the index as of a stated date and is re-checked, not
   assumed, before each stage that consumes new sessions.
3. Reproducing a result from this repository may require a session the archive
   no longer offers. Where that matters, the determinism hashes in
   `docs/correctness.md` at least establish that the session a result came from
   was byte-identical to the one the author replayed.

## Test fixtures

`ex20101224.TEST_ITCH_50`, bundled with the RITCH R package, is a 12,012-message
synthetic file used for the Stage 1 fast census gate. It is a test fixture
distributed with that package and is not fetched from NASDAQ.

`gen_synthetic` writes a deterministic BinaryFILE-framed session for CI, which
needs no download. Its order flow is not a market model and it is not a source
of microstructure results; it covers the framing and decode paths only.
