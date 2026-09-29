"""Compute identical PresentMon 2.x metrics for several captures.

Usage: python compare-presentmon.py [--trim-s 3] [--json out.json] label=path/to/presentmon.csv ...

Rows are filtered to the busiest process in each file. The first --trim-s seconds
are dropped. "Displayed" rows are those with a numeric MsBetweenDisplayChange.
"Real frames" are rows with a numeric MsBetweenSimulationStart (PresentMon
simulation markers; with DLSS-G these mark the game's own frames).
"""
import argparse
import csv
import json
import math
from collections import Counter


def num(value):
    try:
        v = float(value)
    except (TypeError, ValueError):
        return None
    return v if math.isfinite(v) else None


def pct(values, p):
    if not values:
        return None
    s = sorted(values)
    k = (len(s) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    return s[lo] if lo == hi else s[lo] + (s[hi] - s[lo]) * (k - lo)


def summarize(path, trim_s):
    with open(path, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    pid = Counter(r["ProcessID"] for r in rows).most_common(1)[0][0]
    rows = [r for r in rows if r["ProcessID"] == pid]
    t = [num(r["TimeInQPC"]) for r in rows]
    qpc_per_s = 1e7
    t0 = t[0]
    rows = [r for r, ti in zip(rows, t) if (ti - t0) / qpc_per_s >= trim_s]
    ts = [num(r["TimeInQPC"]) for r in rows]
    span = (ts[-1] - ts[0]) / qpc_per_s
    disp = [num(r["MsBetweenDisplayChange"]) for r in rows]
    disp = [d for d in disp if d is not None]
    until = [v for v in (num(r["MsUntilDisplayed"]) for r in rows) if v is not None]
    pcl = [v for v in (num(r.get("MsPCLatency")) for r in rows) if v is not None]
    sims = [v for v in (num(r.get("MsBetweenSimulationStart")) for r in rows) if v is not None]
    presents = [v for v in (num(r["MsBetweenPresents"]) for r in rows) if v is not None]
    r2 = lambda v: None if v is None else round(v, 2)
    return {
        "file": path,
        "process_id": pid,
        "span_s": r2(span),
        "presents_per_s": r2(len(rows) / span),
        "displayed_per_s": r2(len(disp) / span),
        "real_frames_per_s": r2(len(sims) / span) if sims else None,
        "MsBetweenPresents": {"p50": r2(pct(presents, 50)), "p95": r2(pct(presents, 95)), "max": r2(max(presents))},
        "MsBetweenDisplayChange": {
            "p50": r2(pct(disp, 50)), "p95": r2(pct(disp, 95)), "p99": r2(pct(disp, 99)), "max": r2(max(disp)),
            "ge25ms": sum(1 for d in disp if d >= 25.0),
            "ge25ms_share": r2(100.0 * sum(1 for d in disp if d >= 25.0) / len(disp)),
        },
        "MsUntilDisplayed": {"p50": r2(pct(until, 50)), "p95": r2(pct(until, 95))},
        "MsPCLatency": {"p50": r2(pct(pcl, 50)), "p95": r2(pct(pcl, 95)), "p99": r2(pct(pcl, 99))} if pcl else None,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trim-s", type=float, default=3.0)
    ap.add_argument("--json")
    ap.add_argument("captures", nargs="+")
    a = ap.parse_args()
    out = {"trim_s": a.trim_s, "captures": {}}
    for item in a.captures:
        label, path = item.split("=", 1)
        out["captures"][label] = summarize(path, a.trim_s)
    text = json.dumps(out, indent=2)
    print(text)
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            f.write(text + "\n")


if __name__ == "__main__":
    main()
