"""Plot wavetable vs std::sin benchmark sweep.

x = total oscillators (voices * partials), log scale
y = time in ms, log scale
Two lines: wavetable lookup and std::sin.

Each total-oscillator value may be produced by several (voices, partials)
combinations; we draw the mean as the line and the raw points as faint markers.

Usage:  python plot_sweep.py [sweep.csv] [out.png]
"""
import csv
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

csv_path = sys.argv[1] if len(sys.argv) > 1 else "sweep.csv"
out_path = sys.argv[2] if len(sys.argv) > 2 else "sweep.png"
opt_flag = sys.argv[3] if len(sys.argv) > 3 else "-O3"

# method -> list of (total, ms)
points = defaultdict(list)
with open(csv_path, newline="") as f:
    for row in csv.DictReader(f):
        points[row["method"]].append((int(row["total"]), float(row["ms"])))

styles = {
    "std_sin":   dict(label="std::sin",        color="#d6336c", marker="o"),
    "wavetable": dict(label="sine wavetable",  color="#1c7ed6", marker="s"),
}

fig, ax = plt.subplots(figsize=(9, 6))

for method, raw in points.items():
    st = styles.get(method, dict(label=method, color="gray", marker="."))

    # mean time per total-oscillator value
    by_total = defaultdict(list)
    for total, ms in raw:
        by_total[total].append(ms)
    totals = sorted(by_total)
    means = [sum(by_total[t]) / len(by_total[t]) for t in totals]

    # faint raw scatter (shows spread across voice/partial combos)
    xs = [t for t, _ in raw]
    ys = [m for _, m in raw]
    ax.scatter(xs, ys, s=14, color=st["color"], alpha=0.18, edgecolors="none", zorder=1)

    # the line
    ax.plot(totals, means, color=st["color"], marker=st["marker"], markersize=5,
            linewidth=2, label=st["label"], zorder=3)

ax.set_xscale("log", base=2)
ax.set_yscale("log")
ax.set_xlabel("Total oscillators  (voices × partials)")
ax.set_ylabel("Time per measurement (ms)")
ax.set_title(f"Wavetable lookup vs std::sin  —  g++ {opt_flag}")
ax.grid(True, which="both", linestyle=":", alpha=0.5)
ax.legend(frameon=True)

# label x ticks with the actual powers of two present in the data
all_totals = sorted({t for raw in points.values() for t, _ in raw})
ax.set_xticks(all_totals)
ax.set_xticklabels([str(t) for t in all_totals], rotation=45, fontsize=8)

fig.tight_layout()
fig.savefig(out_path, dpi=150)
print(f"wrote {out_path}")
