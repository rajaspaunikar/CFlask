#!/usr/bin/env python3
"""
plot_split.py - draw throughput and response time as two separate images

Usage:  python3 scripts/plot_split.py plots/loadtest1.data
Output: plots/loadtest1_throughput.jpg
        plots/loadtest1_latency.jpg     (and .png for both)

Config labels: s = single-threaded, m = thread per request, tN = thread pool
of N. Old labels (single, N) are also accepted.
"""
import sys
from collections import defaultdict
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def normalize(k):
    if k == "single":
        return "s"
    if k.isdigit():
        return "t" + k
    return k


def order(k):
    return ({"s": 0, "m": 1, "t": 2}[k[0]], int(k[1:] or 0))


def label(k):
    if k == "s":
        return "single-threaded"
    if k[0] == "t":
        return f"thread pool = {k[1:]}"
    return "thread per request"


STYLE = {"s": "--", "m": ":", "t": "-"}

path = sys.argv[1]
title_url = ""
loadgen = "wrk"
series = defaultdict(list)          # config -> [(conc, rps, lat)]

with open(path) as f:
    for line in f:
        if line.startswith("# loadgen:"):
            loadgen = line.split(":", 1)[1].strip()
        elif line.startswith("# client cmd:") and " ab " in line:
            loadgen = "ab"
        if line.startswith("# url:"):
            title_url = line.split(":", 1)[1].strip().split("127.0.0.1")[-1]
        if line.startswith("#") or not line.strip():
            continue
        cfg, c, rps, lat, _ = line.split()
        if rps == "NA" or lat == "NA":
            continue
        series[normalize(cfg)].append((int(c), float(rps), float(lat)))

base = path.rsplit(".", 1)[0]

# (output suffix, column index, y-axis label, title)
GRAPHS = [
    ("throughput", 1, "Throughput (requests/sec)", "Throughput vs load"),
    ("latency",    2, "Mean response time (ms)",   "Response time vs load"),
]

for suffix, col, ylabel, title in GRAPHS:
    fig, ax = plt.subplots(figsize=(7, 5))
    for cfg in sorted(series, key=order):
        pts = sorted(series[cfg])
        ax.plot([p[0] for p in pts], [p[col] for p in pts],
                STYLE[cfg[0]], marker="o", label=label(cfg))
    ax.set_xscale("log", base=2)
    ax.set_ylim(bottom=0)
    ax.set_xlabel(f"Concurrent connections ({loadgen} -c)")
    ax.set_ylabel(ylabel)
    ax.set_title(f"{title}: {title_url}")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    out = f"{base}_{suffix}"
    fig.savefig(out + ".jpg", dpi=150)
    fig.savefig(out + ".png", dpi=150)
    plt.close(fig)
    print("wrote", out + ".jpg")