# CSE321 Project #1 — B-tree / B\*-tree / B+-tree

Implementation and analysis of three tree-based index structures over 100,000
student records.

## Layout

```
project/
├── src/
│   ├── btree.hpp        # B-tree of minimum degree d
│   ├── bplustree.hpp    # B+-tree (linked leaves, range scan)
│   ├── bstartree.hpp    # B*-tree (redistribute then 2-to-3 split)
│   ├── record.hpp       # StudentRecord, Key, RID
│   ├── csv_loader.hpp   # CSV → std::vector<StudentRecord>
│   ├── benchmark.hpp    # Timer
│   └── main.cpp         # experiment driver
├── scripts/
│   └── plot.py          # results/*.csv → results/plots/*.png
├── data/
│   └── student.csv      # 100k student records
├── results/             # experiment CSVs + PNG plots
├── requirements.txt     # Python plotting deps (matplotlib, pandas, numpy)
├── run.sh               # one-step reproducer (recommended)
├── Makefile
└── README.md
```

## Requirements

- C++17 compiler (`g++` ≥ 9 or `clang++`). Tested with `g++ 13` / Apple `clang 15`.
- GNU `make`.
- `python3` ≥ 3.9 (only for plotting; the C++ experiments don't need it).

The Python plotting deps are pinned in `requirements.txt`
(matplotlib ≥ 3.5, pandas ≥ 2.0, numpy ≥ 1.23). `run.sh` creates a local
virtualenv at `.venv/` and installs them automatically — no system-wide
`pip install` is required, so the macOS PEP 668 restriction is avoided.

## Build & run

The recommended path is the one-step bootstrap script:

```sh
./run.sh
```

It performs, in order:

1. Create `.venv/` and `pip install -r requirements.txt` if missing.
2. Compile `src/main.cpp` to `bin/btree_exp`.
3. Run all experiments → `results/*.csv` (7 files).
4. Render every plot → `results/plots/*.png` (18 files).

End-to-end takes ~30 seconds on an Apple M-class Mac.

If you prefer fine-grained control, the underlying `Makefile` exposes
each step separately:

```sh
make venv             # create .venv and install deps from requirements.txt
make run              # build C++ binary and run experiments only
make plots            # render plots from existing results/*.csv
make all-experiments  # run + plots
```

The compiled binary can also be invoked directly:

```sh
./bin/btree_exp data/student.csv results
.venv/bin/python scripts/plot.py results
```

## Experiments

The driver runs four experiment groups:

1. **Basic order sweep** (3 trials × `d ∈ {3, 4, 5, 7, 10, 15, 20, 30, 50}`)
   - Insertion: time, total splits, final utilization, height, node count
   - Point search: 10,000 random keys, mean μs/query
   - Range query: avg GPA & height of male students with IDs in
     [202000000, 202100000]
   - Deletion: 10% then 20% of records, time + post-delete utilization
   → `insertion.csv`, `search.csv`, `range.csv`, `deletion.csv`
2. **Insert-ordering effect** (`d=10`, orderings: sorted / reverse / csv /
   3 shuffles) — shows skew impact on B-tree.
   → `insert_ordering.csv`
3. **Range-width sweep** (`d=10`, widths 10²–10⁷, 3 trials) — log-log
   scaling of range query time vs window size.
   → `range_width.csv`
4. **Splits trajectory** (`d=10`, shuffled) — cumulative splits, height and
   utilization sampled every 10k inserted records.
   → `splits_traj.csv`

The PDF example range query uses 8-digit IDs (20200000–20210000), but the
provided CSV uses 9-digit Student IDs (2020xxxxx–2026xxxxx). The range is
scaled accordingly to [202000000, 202100000].

## Plots

`make plots` produces 18 PNGs in `results/plots/`:

| #  | file                              | what it shows                         |
|----|-----------------------------------|---------------------------------------|
| 01 | `01_insertion_time.png`           | insertion time vs `d`                 |
| 02 | `02_insertion_splits.png`         | total splits vs `d`                   |
| 03 | `03_insertion_utilization.png`    | final utilization vs `d`              |
| 04 | `04_insertion_height.png`         | tree height vs `d`                    |
| 05 | `05_insertion_nodes.png`          | node count vs `d` (log-y)             |
| 06 | `06_search_latency.png`           | mean point-search latency vs `d`      |
| 07 | `07_range_time.png`               | range query time vs `d`               |
| 08 | `08_deletion_time_*.png`          | deletion time per batch               |
| 09 | `09_deletion_util_*.png`          | post-deletion utilization             |
| 10 | `10_ordering_time.png`            | insert time × ordering (bars)         |
| 11 | `11_ordering_splits.png`          | splits × ordering                     |
| 12 | `12_ordering_utilization.png`     | utilization × ordering                |
| 13 | `13_range_width_time.png`         | range time vs window width (log-log)  |
| 14 | `14_range_width_hits.png`         | result count vs window width          |
| 15 | `15_splits_trajectory.png`        | cumulative splits during insertion    |
| 16 | `16_height_trajectory.png`        | tree-height growth during insertion   |

## Order convention

The implementation uses **Cormen's minimum degree** `d`. A node has at most
`2d − 1` keys and at least `d − 1` keys (root may hold fewer).

| symbol     | B-tree / B+-tree | B\*-tree                                       |
|------------|------------------|------------------------------------------------|
| `max_keys` | `2d − 1`         | `2d − 1`                                       |
| `min_keys` | `d − 1`          | insert: `⌈2(2d−1)/3⌉` (2/3 fill); delete: `d − 1` |

For `d ∈ {3, 5, 10}`, that is `max_keys = 5 / 9 / 19`.

This convention makes the merge step well-defined: two minimum-sized siblings
plus their separator combine into exactly `2(d − 1) + 1 = 2d − 1` keys.

## Implementation notes

- **B-tree** (`btree.hpp`): top-down preemptive split during insertion;
  internal nodes carry RIDs so search may terminate early.
- **B+-tree** (`bplustree.hpp`): bottom-up split with sibling pointer in
  leaves; `range(lo, hi, cb)` walks linked leaves once the entry leaf is
  located.
- **B\*-tree** (`bstartree.hpp`): on overflow, attempts redistribution with a
  non-full sibling first; falls back to a 2-to-3 split (two full nodes +
  parent separator → three new nodes + two promoted separators) only when
  no sibling has room. Deletion uses the relaxed B-tree threshold so that
  the merge stays well-defined; the 2/3 fill invariant is therefore
  guaranteed only on the insertion path.

