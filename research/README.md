# research

Analysis scripts, not notebooks. Each is a command-line program that takes an
input directory and writes figures and a printed summary, so a result can be
regenerated from a commit rather than from a kernel someone left running.

```sh
cmake --build --preset release
./build/release/export_micro --symbols 50 --sample-ms 1000 \
    --out results/micro-bx data/20190130.BX_ITCH_50
python3 research/microstructure.py results/micro-bx --venue BX --date 2019-01-30
```

`export_micro` writes CSV aggregates to `results/`, which is gitignored
because it derives from market data. `microstructure.py` writes figures to
`docs/figures/`, which is committed: the figures are aggregates over thousands
of symbols and carry no market data.

Every figure is stamped with its venue and session. One venue per figure: BX
is taker-maker and NASDAQ maker-taker, they attract different order flow, and
drawing them on the same axes would invite the pooling `docs/design.md`
record 022 forbids.

## Requirements

Python 3.11 or newer, with `pandas`, `numpy`, `matplotlib` and `arch`. `arch`
supplies the Politis-White block-length selector the pre-registration fixes
for the stationary bootstrap.

```sh
python3 -m pip install pandas numpy matplotlib arch
```
