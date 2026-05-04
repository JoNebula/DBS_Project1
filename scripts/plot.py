#!/usr/bin/env python3
"""Read results/*.csv produced by bin/btree_exp and render PNG plots
into results/plots/.

Usage:
    .venv/bin/python scripts/plot.py [results_dir]
"""
import os
import sys
import pandas as pd
import matplotlib.pyplot as plt

TREES   = ["Btree", "Bstartree", "Bplustree"]
LABELS  = {"Btree": "B-tree", "Bstartree": "B*-tree", "Bplustree": "B+-tree"}
COLORS  = {"Btree": "#1f77b4", "Bstartree": "#d62728", "Bplustree": "#2ca02c"}
MARKERS = {"Btree": "o", "Bstartree": "s", "Bplustree": "^"}


def aggregate(df, x, y):
    """Return (x-values, mean, std) per (tree, x). Mean/std across trials."""
    g = df.groupby(["tree", x])[y].agg(["mean", "std"]).reset_index()
    return g


def line_per_tree(df, x_col, y_col, ylabel, title, out_path,
                  log_y=False, log_x=False):
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    agg = aggregate(df, x_col, y_col)
    for t in TREES:
        sub = agg[agg.tree == t].sort_values(x_col)
        if sub.empty:
            continue
        ax.errorbar(sub[x_col], sub["mean"],
                    yerr=sub["std"].fillna(0),
                    label=LABELS[t], color=COLORS[t], marker=MARKERS[t],
                    capsize=3, linewidth=1.6, markersize=6)
    ax.set_xlabel(x_col)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    if log_y: ax.set_yscale("log")
    if log_x: ax.set_xscale("log")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_path, dpi=140)
    plt.close(fig)
    print(f"  wrote {out_path}")


def grouped_bar(df, group_col, y_col, ylabel, title, out_path,
                group_order=None):
    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    groups = group_order or sorted(df[group_col].unique())
    n = len(groups); m = len(TREES)
    width = 0.8 / m
    x_idx = list(range(n))

    for i, t in enumerate(TREES):
        vals, errs = [], []
        for g in groups:
            sub = df[(df.tree == t) & (df[group_col] == g)][y_col]
            vals.append(sub.mean() if len(sub) else 0.0)
            errs.append(sub.std() if len(sub) > 1 else 0.0)
        offsets = [x + (i - (m - 1) / 2) * width for x in x_idx]
        ax.bar(offsets, vals, width=width, yerr=errs, capsize=3,
               label=LABELS[t], color=COLORS[t], edgecolor="black", linewidth=0.4)
    ax.set_xticks(x_idx)
    ax.set_xticklabels(groups)
    ax.set_xlabel(group_col)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.grid(True, axis="y", alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_path, dpi=140)
    plt.close(fig)
    print(f"  wrote {out_path}")


def main():
    results_dir = sys.argv[1] if len(sys.argv) > 1 else "results"
    plots_dir = os.path.join(results_dir, "plots")
    os.makedirs(plots_dir, exist_ok=True)

    # ---------- Basic order-sweep plots ----------
    ins = pd.read_csv(os.path.join(results_dir, "insertion.csv"))
    line_per_tree(ins, "order", "time_ms",
                  "insertion time (ms)",
                  "Insertion time vs order  (100k records, 3 trials)",
                  os.path.join(plots_dir, "01_insertion_time.png"))
    line_per_tree(ins, "order", "splits",
                  "total splits",
                  "Number of node splits during insertion",
                  os.path.join(plots_dir, "02_insertion_splits.png"))
    line_per_tree(ins, "order", "utilization",
                  "node utilization (filled / max_keys)",
                  "Final tree node utilization",
                  os.path.join(plots_dir, "03_insertion_utilization.png"))
    line_per_tree(ins, "order", "height",
                  "tree height",
                  "Final tree height",
                  os.path.join(plots_dir, "04_insertion_height.png"))
    line_per_tree(ins, "order", "node_count",
                  "node count",
                  "Number of nodes after inserting 100k records",
                  os.path.join(plots_dir, "05_insertion_nodes.png"),
                  log_y=True)

    srch = pd.read_csv(os.path.join(results_dir, "search.csv"))
    line_per_tree(srch, "order", "mean_us",
                  "mean latency (μs / query)",
                  "Point-search latency vs order  (10k random queries)",
                  os.path.join(plots_dir, "06_search_latency.png"))

    rng = pd.read_csv(os.path.join(results_dir, "range.csv"))
    line_per_tree(rng, "order", "time_ms",
                  "range-query time (ms)",
                  "Range query: avg GPA/height of male students in [202000000, 202100000]",
                  os.path.join(plots_dir, "07_range_time.png"))

    delete = pd.read_csv(os.path.join(results_dir, "deletion.csv"))
    for batch in ["10pct", "20pct_total"]:
        sub = delete[delete.batch == batch]
        line_per_tree(sub, "order", "time_ms",
                      f"deletion time (ms) — batch={batch}",
                      f"Deletion time vs order ({batch})",
                      os.path.join(plots_dir, f"08_deletion_time_{batch}.png"))
        line_per_tree(sub, "order", "util_after",
                      "utilization after deletion",
                      f"Tree utilization after deleting {batch}",
                      os.path.join(plots_dir, f"09_deletion_util_{batch}.png"))

    # ---------- Insert-ordering experiment ----------
    ord_df = pd.read_csv(os.path.join(results_dir, "insert_ordering.csv"))
    order_groups = ["sorted", "reverse", "csv", "shuffle"]
    grouped_bar(ord_df, "ordering", "time_ms",
                "insertion time (ms)",
                "Insertion time by input ordering (d=10)",
                os.path.join(plots_dir, "10_ordering_time.png"),
                group_order=order_groups)
    grouped_bar(ord_df, "ordering", "splits",
                "splits",
                "Splits by input ordering (d=10)",
                os.path.join(plots_dir, "11_ordering_splits.png"),
                group_order=order_groups)
    grouped_bar(ord_df, "ordering", "utilization",
                "utilization",
                "Final utilization by input ordering (d=10)",
                os.path.join(plots_dir, "12_ordering_utilization.png"),
                group_order=order_groups)

    # ---------- Range-width sweep ----------
    wid = pd.read_csv(os.path.join(results_dir, "range_width.csv"))
    line_per_tree(wid, "width", "time_ms",
                  "range query time (ms)",
                  "Range query time vs window width (d=10)",
                  os.path.join(plots_dir, "13_range_width_time.png"),
                  log_x=True, log_y=True)
    line_per_tree(wid, "width", "hits",
                  "result count",
                  "Range query: result count vs window width (d=10)",
                  os.path.join(plots_dir, "14_range_width_hits.png"),
                  log_x=True, log_y=True)

    # ---------- Splits trajectory ----------
    tr = pd.read_csv(os.path.join(results_dir, "splits_traj.csv"))
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    for t in TREES:
        sub = tr[tr.tree == t].sort_values("n_inserted")
        if sub.empty: continue
        ax.plot(sub["n_inserted"], sub["splits"],
                label=LABELS[t], color=COLORS[t], marker=MARKERS[t],
                linewidth=1.6, markersize=4)
    ax.set_xlabel("records inserted")
    ax.set_ylabel("cumulative splits")
    ax.set_title("Cumulative splits during insertion (d=10, shuffled)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(plots_dir, "15_splits_trajectory.png"), dpi=140)
    plt.close(fig)
    print(f"  wrote {os.path.join(plots_dir, '15_splits_trajectory.png')}")

    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    for t in TREES:
        sub = tr[tr.tree == t].sort_values("n_inserted")
        if sub.empty: continue
        ax.plot(sub["n_inserted"], sub["height"],
                label=LABELS[t], color=COLORS[t], marker=MARKERS[t],
                linewidth=1.6, markersize=4, drawstyle="steps-post")
    ax.set_xlabel("records inserted")
    ax.set_ylabel("tree height")
    ax.set_title("Tree height growth (d=10, shuffled)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(plots_dir, "16_height_trajectory.png"), dpi=140)
    plt.close(fig)
    print(f"  wrote {os.path.join(plots_dir, '16_height_trajectory.png')}")

    print(f"\nAll plots in {plots_dir}/")


if __name__ == "__main__":
    main()
