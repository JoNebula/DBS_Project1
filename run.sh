#!/usr/bin/env bash
# One-step reproducer: sets up the Python venv, builds the C++ binary,
# runs the four experiment groups, and renders all plots.
#
# Usage:   ./run.sh
# Tested on macOS (Apple silicon) and Linux. Requires g++ (>=9) with
# C++17 support and python3 (>=3.9).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

# ---- 0. Prerequisite check ----------------------------------------------
need() { command -v "$1" >/dev/null 2>&1 || { echo "ERROR: '$1' not found in PATH"; exit 1; }; }
need g++
need python3
need make

echo "[1/4] Setting up Python virtualenv (.venv) from requirements.txt"
if [ ! -d .venv ]; then
  python3 -m venv .venv
fi
.venv/bin/pip install --quiet --upgrade pip
.venv/bin/pip install --quiet -r requirements.txt

echo "[2/4] Building C++ binary (bin/btree_exp)"
mkdir -p bin results
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc src/main.cpp -o bin/btree_exp

echo "[3/4] Running experiments on data/student.csv -> results/*.csv"
./bin/btree_exp data/student.csv results

echo "[4/4] Rendering plots -> results/plots/*.png"
mkdir -p results/plots
.venv/bin/python scripts/plot.py results

echo
echo "Done."
echo "  CSVs : $(ls results/*.csv | wc -l | tr -d ' ') files in results/"
echo "  Plots: $(ls results/plots/*.png | wc -l | tr -d ' ') files in results/plots/"
