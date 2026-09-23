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
`.md5sum` sits in the NASDAQ directory with no `.gz` beside it. Its checksum
cannot be verified, for the reason in *Checksums are listed but not served*
below. **It is not treated as a NASDAQ session because of its name** — see
*Venue is established from content* below, which is the check that decides.

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
| Fast-book overflow rate (256-tick sliding window) | 4.90% |
| Window recenters / levels moved | 703,924 / 11,999,325 |
| Sub-cent prices | 656,931 |
| Trading actions | 8,922 `T`, 22 `P`, 19 `H`, 3 `Q` |
| Crossed / locked observations | 8,580 / 70, all excused (record 036) |

The overflow rate is **more than double BX's 2.37%** at the same window width,
on a venue whose symbols span a far wider price range. The window geometry of
record 033 is unchanged and was not retuned for this session.

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

## Whole-file transfer does not work for the larger sessions

Measured on 2026-09-23, from the development Mac. Progress at the moment each
connection dropped, as a fraction of the advertised length:

| Session | Advertised | att 1 | att 2 | att 3 | att 4 |
|---|---:|---:|---:|---:|---:|
| `01302019.NASDAQ_ITCH50.gz` | 4.76 GB | 1.5% | **67.6%** | 0.4% | 0.4% |
| `03272019.NASDAQ_ITCH50.gz` | 5.51 GB | 0.7% | none | none | none |

**All eight attempts failed.** The pattern is not a connection-duration cap:
attempt 2 carried **3.22 GB** in one connection, so a long transfer is
possible, and the attempts after it died at 0.4% or produced no progress at
all. Degradation across successive requests, with the later ones failing
almost immediately, is the shape of **server-side throttling** rather than a
fixed limit any single connection would hit. Retrying the whole file is
therefore not a strategy for these sessions — each retry starts from zero and
the later retries are the ones most likely to be refused.

The server was not down: a `HEAD` during the same period returned 200 with the
correct `content-length`, `accept-ranges: bytes` and an `etag`.

### Ranges are honoured, so the transfer is chunked

```
GET /ITCH/Nasdaq%20ITCH/01302019.NASDAQ_ITCH50.gz   Range: bytes=0-1023
  HTTP/2 206
  content-range: bytes 0-1023/4764426091
  content-length: 1024
  etag: "29fda95e24b9d41:0"
```

206 with a `Content-Range` exactly matching the request, so
`tools/fetch_chunked.sh` fetches 64 MiB at a time and resumes from whatever it
already has. Measured immediately after the eight whole-file failures, it
sustained about **11.6 MB/s**, which is the same archive answering a different
request shape.

**What each chunk must prove before a byte of it is appended.** This is the
failure the script exists to prevent, and it has already happened here: an
earlier attempt used `curl --continue-at`, a retry returned the *whole* body,
curl appended it to the partial, and the result was **4,038,899,612 bytes
against an advertised 3,524,013,057** — 115% of the file, with a valid gzip
stream at the front, so it looked finished.

- status exactly **206**; a **200 is rejected**, because it is the whole file
  rather than the range asked for;
- `Content-Range` exactly `bytes START-END/TOTAL` for the range requested;
- `ETag` identical to the first chunk's, so the file cannot change mid-fetch;
- body length exactly `END-START+1`.

`tests/chunk_negative.cmake` drives each of those rejections offline, plus a
positive control, plus the corrupted-byte case below.

### What the gzip trailer establishes, and what it does not

A gzip member ends with a **CRC-32 over the entire uncompressed stream** and
the uncompressed length. `gzip -t` inflates the whole file and checks both, so
it is a genuine **end-to-end integrity check on the transfer**: every byte
took part in the CRC, and a single flipped byte anywhere fails it without
changing the file's length. That matters here precisely because length is what
a chunked assembly is most likely to get right while getting content wrong.
`tests/chunk_negative.cmake` flips one byte mid-stream and requires
verification to fail at unchanged length.

**It establishes:** that the bytes now on disk inflate to a stream whose
CRC-32 and length match what the compressor recorded when the file was
created. Truncation, appended data, a dropped or duplicated chunk, and
in-flight corruption are all caught.

**It does not establish:** that this is the file the publisher intended to
serve. A CRC-32 is a 32-bit error-detecting code, not a cryptographic digest —
it is trivial to construct a different file with the same CRC, so it carries
no authenticity claim at all. And it can only compare the archive against
*itself*: if the stored file were replaced with a different, internally
consistent gzip, every check here would pass. **No session in this repository
has a publisher-verified digest**, because every `.md5sum` URL 404s. The
SHA-256 values recorded here are computed on arrival and establish that a
later copy is the same bytes — not that those bytes are the ones NASDAQ
served.

### Download method, per file

| Session | Method | Outcome |
|---|---|---|
| `20190130.BX_ITCH_50.gz` | whole file | completed |
| `12302019.NASDAQ_ITCH50.gz` | whole file, after two corrupted attempts | completed; see *three attempts to get it right* |
| `20190530.PSX_ITCH_50.gz` | whole file (0.54 GB) | completed |
| `01302019.NASDAQ_ITCH50.gz` | **chunked**, after 4 whole-file failures | in progress |
| `03272019.NASDAQ_ITCH50.gz` | **chunked**, after 4 whole-file failures | pending |
| `07302019.NASDAQ_ITCH50.gz` | **chunked** | pending |
| `05302019`, `08302019`, `S101819` | pending the Linux throughput measurement | pending |

## Fetching the remaining development sessions on a second machine

Three sessions are fetched on the Linux box and transferred; three are fetched
here. The split is recorded so the same file is not pulled twice, and so that
a reader can tell which machine produced which digest.

| Session | Machine | Directory |
|---|---|---|
| 2019-01-30, 2019-03-27, 2019-07-30 | development Mac | `Nasdaq ITCH/` |
| 2019-05-30, 2019-08-30, 2019-10-18 | Linux box | `Nasdaq PSX ITCH/`, `Nasdaq ITCH/`, `Nasdaq ITCH/` |

**Whole file, one connection per file.** Range requests are why two earlier
downloads produced a file that looked finished and was not: this archive
answers `HEAD` with `Range` as 416 while honouring `Range` on `GET`, and a
retry that returned the whole body was appended to a partial one, giving
4,038,899,612 bytes against an advertised 3,524,013,057. `--continue-at` is
therefore not used anywhere, and neither is any parallel-chunk downloader.

```sh
# On the Linux box, from the repository root.
BASE=https://emi.nasdaq.com/ITCH
mkdir -p data

fetch() {                       # fetch <directory> <file>
  dir=$(printf '%s' "$1" | sed 's/ /%20/g'); file=$2
  want=$(curl -sI --max-time 30 "$BASE/$dir/$file" | tr -d '\r' \
         | awk 'tolower($1)=="content-length:" {print $2}' | tail -1)
  echo "$file: advertised $want bytes"
  curl --fail --location --max-time 14400 --speed-limit 1024 --speed-time 120 \
       --retry 0 --output "data/.$file.partial" "$BASE/$dir/$file"
  got=$(wc -c < "data/.$file.partial" | tr -d ' ')
  [ "$got" = "$want" ] || { echo "SHORT: $got of $want; delete and retry" >&2; return 1; }
  mv "data/.$file.partial" "data/$file"
  sh tools/verify_archive.sh --no-published-digest "data/$file" "$want"
  sha256sum "data/$file" | tee -a data/linux_digests.txt
}

fetch "Nasdaq PSX ITCH" 05302019.NASDAQ_ITCH50.gz     # note: NASDAQ file, PSX directory
fetch "Nasdaq ITCH"     08302019.NASDAQ_ITCH50.gz
fetch "Nasdaq ITCH"     S101819-v50.txt.gz

# Unpack and record the unpacked digest too: the transfer check below is run
# against whichever file is actually moved.
for f in 05302019.NASDAQ_ITCH50 08302019.NASDAQ_ITCH50 S101819-v50.txt; do
  gunzip -k "data/$f.gz"
  sha256sum "data/$f" | tee -a data/linux_digests.txt
done
```

`--retry 0` is deliberate: a retry inside `curl` is what produced the appended
file. A failed transfer is restarted by hand, from zero, after deleting the
partial.

### On arrival, the transfer is checked before the file counts as local

A file that crossed a network is not the file that was verified until its
digest is recomputed on this side and matched **exactly**. Length is not
enough — a truncation that lands on a whole number of blocks has the right
length for a shorter file, and the gzip check passed on the *other* machine.

```sh
# On the Mac, for each transferred file.
shasum -a 256 data/<file>              # must equal the value in linux_digests.txt
sh tools/verify_archive.sh --no-published-digest data/<file>.gz <advertised bytes>
./build/release/census --sha256 data/<file>
./build/release/venue_profile data/<file>
./build/release/replay data/<file>
```

**All five must pass before a session is treated as local and verified:** the
digest matches the Linux-side value exactly, the archive checks pass, the
census is clean and ends on System Event `'C'`, the venue profile returns
**NASDAQ**, and the differential replay reports `RESULT: identical`. A
mismatch in the first is a transfer failure and the file is re-sent, not
re-verified.

## Venue is established from content, not from where the file was filed

The directory layout is not reliable. `05302019.NASDAQ_ITCH50.gz` sits in
`Nasdaq PSX ITCH/` beside genuine `*.PSX_ITCH_50.gz` files, and
`S101819-v50.txt.gz` follows neither naming convention. Venue decides the fee
model, and therefore what may be pooled with what (`docs/design.md` record
037), so a session's venue is established by `tools/venue_profile` from the
bytes.

**The discriminator is the auction.** NASDAQ runs an opening and a closing
cross and disseminates Net Order Imbalance Indicators throughout the day;
BX and PSX run neither. That is a difference of kind rather than of degree,
which is what makes it a verdict instead of evidence.

| Session | Messages | NOII `I` | Cross `O` | Cross `C` | Paired shares | Verdict |
|---|---:|---:|---:|---:|---:|---|
| `12302019.NASDAQ_ITCH50` | 268,744,780 | 4,024,315 | 8,906 | 8,906 | 29,877,544,680 | **NASDAQ** |
| `20190530.PSX_ITCH_50` | 42,541,827 | 0 | 0 | 0 | 0 | not NASDAQ |
| `20190130.BX_ITCH_50` | 82,841,542 | 0 | 0 | 0 | 0 | not NASDAQ |

The two reference venues return **exactly zero** on all three auction
measures, and the NASDAQ reference returns one opening and one closing cross
for **every one of its 8,906 listed symbols**. Nothing sits between the two
outcomes, so a session that is ambiguous on this test is a session worth
stopping over; `venue_profile` exits nonzero and says so.

A first, weaker signal agrees. The same-date PSX file `20190530.PSX_ITCH_50.gz`
is **0.54 GB** while `05302019.NASDAQ_ITCH50.gz` is **3.95 GB**, which is the
range the other NASDAQ development sessions occupy (3.5–5.6 GB) and far above
the PSX range. Size is suggestive and is not the test.

**Verdicts for the development set** are recorded here as each session lands.
A session that does not return **NASDAQ** is removed from the development set
on provenance grounds and the registration's session list is amended before
any gated computation.

| Session | Filed under | Verdict | Profiled |
|---|---|---|---|
| 2019-12-30 `12302019.NASDAQ_ITCH50` | `Nasdaq ITCH/` | **NASDAQ** | 2026-09-23 |
| 2019-01-30 `01302019.NASDAQ_ITCH50` | `Nasdaq ITCH/` | *pending download* | — |
| 2019-03-27 `03272019.NASDAQ_ITCH50` | `Nasdaq ITCH/` | *pending download* | — |
| 2019-05-30 `05302019.NASDAQ_ITCH50` | **`Nasdaq PSX ITCH/`** | *pending download* | — |
| 2019-07-30 `07302019.NASDAQ_ITCH50` | `Nasdaq ITCH/` | *pending download* | — |
| 2019-08-30 `08302019.NASDAQ_ITCH50` | `Nasdaq ITCH/` | *pending download* | — |
| 2019-10-18 `S101819-v50.txt` | `Nasdaq ITCH/` | *pending download* | — |

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
