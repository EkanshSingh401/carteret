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
| 2019-01-30 | `Nasdaq ITCH/01302019.NASDAQ_ITCH50.gz` | 4.76 GB | unassigned |
| 2019-03-27 | `Nasdaq ITCH/03272019.NASDAQ_ITCH50.gz` | 5.51 GB | unassigned |
| 2019-05-30 | `Nasdaq PSX ITCH/05302019.NASDAQ_ITCH50.gz` | 4.25 GB | unassigned |
| 2019-07-30 | `Nasdaq ITCH/07302019.NASDAQ_ITCH50.gz` | 3.66 GB | unassigned |
| 2019-08-30 | `Nasdaq ITCH/08302019.NASDAQ_ITCH50.gz` | 4.08 GB | unassigned |
| 2019-10-18 | `Nasdaq ITCH/S101819-v50.txt.gz` | 3.95 GB | unassigned |
| 2019-10-30 | `Nasdaq ITCH/10302019.NASDAQ_ITCH50.gz` | 3.87 GB | unassigned |
| 2019-12-30 | `Nasdaq ITCH/12302019.NASDAQ_ITCH50.gz` | 3.52 GB | **development** |
| 2020-01-30 | `Nasdaq ITCH/01302020.NASDAQ_ITCH50.gz` | 5.60 GB | unassigned |

The 2019-05-30 NASDAQ session is filed under the PSX directory; the matching
`.md5sum` sits in the NASDAQ directory with no `.gz` beside it. The file is
named `NASDAQ_ITCH50` and is treated as a NASDAQ session. Its checksum cannot
be verified, for the reason in *Checksums are listed but not served* below.

Assignment of these sessions to development and held-out sets happens in
Stage 8, before any feature code is written, and is recorded in this table.
Held-out sessions are not downloaded until the registration commit exists.

**2019-12-30 is already spent and cannot be held out.** It was fetched on
2026-09-22 for the Stage 3 correctness gate, which needs a NASDAQ session
because BX contains no auction messages — `I` and `Q` are both absent from
every BX session, so the opening and closing cross paths have no coverage from
BX at all. Downloading it is what disqualifies it: a held-out session is one
nobody has looked at, and this one has been replayed. It is marked development
here rather than left unassigned so that the Stage 8 split cannot quietly
assume it is available.

The remaining eight NASDAQ sessions are untouched and stay that way until the
Stage 8 split is recorded.

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
| Framing | length-prefixed, **no zero-length terminator** |
| First / last timestamp | 03:06:49 / 19:05:00 |
| Types present | `S R H Y L V A F E C X D U P B N` (16 of 23) |
| Types absent | `I J K Q W h O` |
| `N` (RPII) | 8,301,264 — 10.0% of the session |

Per-type counts agree exactly with `RITCH::count_messages()`; see
`docs/correctness.md`. The absent types mean this session establishes nothing
about the `I`, `J`, `K`, `Q`, `W`, `h` or `O` layouts, whose only coverage is
the byte fixtures. BX runs no opening or closing cross, which accounts for `I`
and `Q`.

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

## Continuing an interrupted download, and how curl can report success on a third of a file

The archive drops connections mid-transfer: fetching
`12302019.NASDAQ_ITCH50.gz` (3,524,013,057 bytes) failed once with a
connection reset at 29% and once with a stall at 38%. Continuing a partial
transfer is therefore necessary, and it needs care.

**Measured on 2026-09-22:**

| Request | Response |
|---|---|
| `HEAD` | 200, `content-length: 3524013057`, `accept-ranges: bytes` |
| `HEAD` with `Range` | **416**, `content-range: bytes */0` |
| `GET` with `Range: bytes=0-0` | 206, `content-range: bytes 0-0/3524013057` |
| `GET` with a mid-file `Range` | 206, correct `content-range` |

So the server does honour range requests, but only on `GET`; it answers `HEAD`
with a `Range` header as though the range were unsatisfiable. A probe for
range support must therefore use `GET`, or it will conclude that continuing a
partial transfer is impossible when it is not.

**The failure that matters is separate from that.** On the first such
attempt, curl was configured with `--continue-at -` together with `--retry`
and a slow-transfer timeout. It continued correctly from the partial file,
transferred to 38.5%, timed out, retried — and then immediately printed
`100.0%` and **exited zero, with the file back at exactly the byte offset it
had started from**, 29% of the session. The bytes fetched after that offset
were discarded and the transfer was declared complete.

`curl` exiting zero is therefore not evidence that a file is whole.
`tools/fetch_data.sh` checks the downloaded size against the advertised
`content-length` and re-runs until it matches, and tests the gzip stream
before unpacking. The gzip test is the backstop: `gunzip` on a truncated
stream produces a plausible prefix of a session rather than failing at the
start, so a short download would otherwise surface much later as a wrong
message count with no obvious cause.

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
