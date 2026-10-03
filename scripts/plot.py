#!/usr/bin/env python3
"""
plot.py - draw throughput and response time vs concurrency from a .data file

Usage:  python3 scripts/plot.py plots/loadtest1.data
Output: plots/loadtest1.jpg (and .png)

Config labels: s = single-threaded, tN = thread pool of N,
m = thread per request. Old labels (single, N) are also accepted.
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


STYLE = {"s": "--", "t": "-", "m": ":"}

path = sys.argv[1]
title_url = ""
loadgen = "wrk"
series = defaultdict(list)          # config -> [(conc, rps, lat)]

with open(path) as f:
    for line in f:
        if line.startswith("# loadgen:"):
            loadgen = line.split(":", 1)[1].strip()
        elif line.startswith("# client cmd:") and " ab " in line:
            loadgen = "ab"          # older data files without a loadgen line
        if line.startswith("# url:"):
            title_url = line.split(":", 1)[1].strip().split("127.0.0.1")[-1]
        if line.startswith("#") or not line.strip():
            continue
        cfg, c, rps, lat, _ = line.split()
        if rps == "NA" or lat == "NA":
            continue
        series[normalize(cfg)].append((int(c), float(rps), float(lat)))

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5))
for cfg in sorted(series, key=order):
    pts = sorted(series[cfg])
    xs = [p[0] for p in pts]
    ax1.plot(xs, [p[1] for p in pts], STYLE[cfg[0]], marker="o", label=label(cfg))
    ax2.plot(xs, [p[2] for p in pts], STYLE[cfg[0]], marker="o", label=label(cfg))

for ax in (ax1, ax2):
    ax.set_xscale("log", base=2)
    ax.set_ylim(bottom=0)
    ax.set_xlabel(f"Concurrent connections ({loadgen} -c)")
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