#!/usr/bin/env python3
"""
plot.py - draw throughput and response time vs concurrency from a .data file

Usage: python3 scripts/plot.py plots/loadtest1.data
Output: plots/loadtest1.jpg (and .png)
"""
import sys
from collections import defaultdict
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

path = sys.argv[1]
title_url = ""
series = defaultdict(list)          # threads -> [(conc, rps, lat)]

with open(path) as f:
    for line in f:
        if line.startswith("# url:"):
            title_url = line.split(":", 1)[1].strip().split("127.0.0.1")[-1]
        if line.startswith("#") or not line.strip():
            continue
        t, c, rps, lat, _ = line.split()
        if rps == "NA" or lat == "NA":
            continue
        series[t].append((int(c), float(rps), float(lat)))

def order(k):
    return (0, 0) if k == "single" else (1, int(k))

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5))
for t in sorted(series, key=order):
    pts = sorted(series[t])
    label = "single-threaded" if t == "single" else f"thread pool = {t}"
    style = "--" if t == "single" else "-"
    xs = [p[0] for p in pts]
    ax1.plot(xs, [p[1] for p in pts], style, marker="o", label=label)
    ax2.plot(xs, [p[2] for p in pts], style, marker="o", label=label)

for ax in (ax1, ax2):
    ax.set_xscale("log", base=2)
    ax.set_xlabel("Concurrent requests (ab -c)")
    ax.grid(True, alpha=0.3)
    ax.legend()
ax1.set_ylabel("Throughput (requests/sec)")
ax1.set_title("Throughput vs load")
ax2.set_ylabel("Mean response time (ms)")
ax2.set_title("Response time vs load")
fig.suptitle(f"cflask load test: {title_url}")
fig.tight_layout()

base = path.rsplit(".", 1)[0]
fig.savefig(base + ".jpg", dpi=150)
fig.savefig(base + ".png", dpi=150)
print("wrote", base + ".jpg")