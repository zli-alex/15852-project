#!/usr/bin/env python3
"""Analyze file_stream logs and produce speedup CSV/plots.

This script intentionally avoids pandas so it can run on older cluster Python
setups with minimal dependencies. It writes CSV summaries unconditionally and
emits PNG plots only when matplotlib is installed.
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import re
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

try:
    import matplotlib.pyplot as plt  # type: ignore
except Exception:
    plt = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Summarize real-dataset scaling logs.")
    parser.add_argument("--logs-dir", default="logs/real_datasets")
    parser.add_argument("--dataset-filter", default=None)
    parser.add_argument("--outdir", default="plots/report")
    parser.add_argument("--prefix", default="real_dataset_scaling")
    return parser.parse_args()


def parse_float(text: str) -> Optional[float]:
    try:
        return float(text)
    except Exception:
        return None


def parse_int(text: str) -> Optional[int]:
    try:
        return int(float(text))
    except Exception:
        return None


def parse_log(path: Path) -> Dict[str, str]:
    row: Dict[str, str] = {"source_name": path.name, "source_log": str(path)}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if "=" not in raw:
            continue
        key, value = raw.split("=", 1)
        key = key.strip()
        if key:
            row[key] = value.strip()
    return row


def median(values: List[float]) -> float:
    return float(statistics.median(values))


def stdev(values: List[float]) -> float:
    if len(values) <= 1:
        return float("nan")
    return float(statistics.stdev(values))


def infer_dataset(name: str) -> str:
    m = re.match(r"^(.*?)_seed\d+_", name)
    return m.group(1) if m else "unknown"


def infer_threads(name: str) -> Optional[int]:
    m = re.search(r"_t(\d+)\.log$", name)
    if not m:
        return None
    return int(m.group(1))


def infer_c_value(name: str, row: Dict[str, str]) -> Optional[int]:
    m = re.search(r"_c(\d+)_t\d+\.log$", name)
    if m:
        return int(m.group(1))
    return parse_int(row.get("palette_multiplier", ""))


def collect_rows(log_paths: Iterable[Path]) -> List[Dict[str, object]]:
    out: List[Dict[str, object]] = []
    for path in log_paths:
        raw = parse_log(path)
        if raw.get("workload") != "file_stream":
            continue
        if parse_int(raw.get("graph_validated", "1")) != 1:
            continue
        if "coloring_validated" in raw and parse_int(raw.get("coloring_validated", "1")) != 1:
            continue
        if parse_float(raw.get("accepted_ratio", "0")) != 1.0:
            continue
        throughput = parse_float(raw.get("throughput_updates_per_second", ""))
        if throughput is None or not math.isfinite(throughput):
            continue
        out.append(
            {
                "dataset": infer_dataset(path.name),
                "engine_name": raw.get("engine_name", ""),
                "seed": parse_int(raw.get("seed", "")),
                "threads": infer_threads(path.name),
                "c_value": infer_c_value(path.name, raw),
                "throughput": throughput,
                "source_name": path.name,
            }
        )
    return out


def aggregate_speed(rows: List[Dict[str, object]], key_fields: Tuple[str, ...]) -> List[Dict[str, object]]:
    groups: Dict[Tuple[object, ...], List[float]] = defaultdict(list)
    for row in rows:
        key = tuple(row.get(k) for k in key_fields)
        groups[key].append(float(row["throughput"]))
    out: List[Dict[str, object]] = []
    for key, vals in sorted(groups.items()):
        record = {k: v for k, v in zip(key_fields, key)}
        record["throughput_median"] = median(vals)
        record["throughput_mean"] = sum(vals) / len(vals)
        record["throughput_std"] = stdev(vals)
        record["n"] = len(vals)
        out.append(record)
    return out


def add_speedup(records: List[Dict[str, object]], group_key: str, x_key: str, baseline_value: int, out_key: str) -> None:
    baseline: Dict[object, float] = {}
    for r in records:
        if r.get(x_key) == baseline_value:
            baseline[r.get(group_key)] = float(r["throughput_median"])
    for r in records:
        b = baseline.get(r.get(group_key))
        r[out_key] = float(r["throughput_median"]) / b if b else float("nan")


def write_csv(path: Path, rows: List[Dict[str, object]], field_order: List[str]) -> None:
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=field_order)
        writer.writeheader()
        for row in rows:
            writer.writerow({k: row.get(k) for k in field_order})


def plot_speedup(
    rows: List[Dict[str, object]],
    x_key: str,
    y_key: str,
    title: str,
    x_label: str,
    y_label: str,
    out_path: Path,
) -> None:
    if plt is None:
        return
    by_dataset: Dict[str, List[Dict[str, object]]] = defaultdict(list)
    for row in rows:
        ds = str(row.get("dataset", "unknown"))
        by_dataset[ds].append(row)
    fig, ax = plt.subplots(figsize=(10, 5))
    for ds, values in sorted(by_dataset.items()):
        vals = sorted(values, key=lambda r: (r.get(x_key) is None, r.get(x_key)))
        xs = [r.get(x_key) for r in vals]
        ys = [r.get(y_key) for r in vals]
        ax.plot(xs, ys, marker="o", linewidth=1.8, label=ds)
    ax.set_title(title)
    ax.set_xlabel(x_label)
    ax.set_ylabel(y_label)
    ax.grid(alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out_path, dpi=180)
    plt.close(fig)


def main() -> int:
    args = parse_args()
    logs_dir = Path(args.logs_dir)
    outdir = Path(args.outdir)
    tables_dir = outdir / "tables"
    figures_dir = outdir / "figures"
    tables_dir.mkdir(parents=True, exist_ok=True)
    figures_dir.mkdir(parents=True, exist_ok=True)

    if not logs_dir.exists():
        raise SystemExit(f"logs directory not found: {logs_dir}")

    log_paths = sorted(logs_dir.glob("*.log"))
    if args.dataset_filter:
        log_paths = [p for p in log_paths if args.dataset_filter in p.name]
    rows = collect_rows(log_paths)
    if not rows:
        raise SystemExit("no matching valid file_stream logs found")

    # par_relaxed thread scaling at c=4
    rel_thread_rows = [r for r in rows if r["engine_name"] == "par_relaxed" and r.get("c_value") == 4 and r.get("threads") is not None]
    rel_thread = aggregate_speed(rel_thread_rows, ("dataset", "threads"))
    add_speedup(rel_thread, "dataset", "threads", 1, "speedup_vs_t1")

    # par_exact thread scaling
    exact_thread_rows = [r for r in rows if r["engine_name"] == "par_exact" and r.get("threads") is not None]
    exact_thread = aggregate_speed(exact_thread_rows, ("dataset", "threads"))
    add_speedup(exact_thread, "dataset", "threads", 1, "speedup_vs_t1")

    # par_relaxed c scaling at threads=8
    rel_c_rows = [r for r in rows if r["engine_name"] == "par_relaxed" and r.get("threads") == 8 and r.get("c_value") is not None]
    rel_c = aggregate_speed(rel_c_rows, ("dataset", "c_value"))
    add_speedup(rel_c, "dataset", "c_value", 4, "speedup_vs_c4")

    rel_thread_csv = tables_dir / f"{args.prefix}_par_relaxed_thread_scaling.csv"
    exact_thread_csv = tables_dir / f"{args.prefix}_par_exact_thread_scaling.csv"
    rel_c_csv = tables_dir / f"{args.prefix}_par_relaxed_c_scaling.csv"

    write_csv(
        rel_thread_csv,
        rel_thread,
        ["dataset", "threads", "throughput_median", "throughput_mean", "throughput_std", "n", "speedup_vs_t1"],
    )
    write_csv(
        exact_thread_csv,
        exact_thread,
        ["dataset", "threads", "throughput_median", "throughput_mean", "throughput_std", "n", "speedup_vs_t1"],
    )
    write_csv(
        rel_c_csv,
        rel_c,
        ["dataset", "c_value", "throughput_median", "throughput_mean", "throughput_std", "n", "speedup_vs_c4"],
    )

    if plt is None:
        print("warning=matplotlib unavailable; wrote CSV tables only")
    else:
        rel_thread_png = figures_dir / f"{args.prefix}_par_relaxed_thread_speedup.png"
        exact_thread_png = figures_dir / f"{args.prefix}_par_exact_thread_speedup.png"
        rel_c_png = figures_dir / f"{args.prefix}_par_relaxed_c_speedup.png"
        plot_speedup(
            rel_thread,
            "threads",
            "speedup_vs_t1",
            "PAR-Relaxed Thread Speedup (c=4, file_stream)",
            "PARLAY_NUM_THREADS",
            "Speedup vs thread=1 (median throughput)",
            rel_thread_png,
        )
        plot_speedup(
            exact_thread,
            "threads",
            "speedup_vs_t1",
            "PAR-Exact Thread Speedup (file_stream)",
            "PARLAY_NUM_THREADS",
            "Speedup vs thread=1 (median throughput)",
            exact_thread_png,
        )
        plot_speedup(
            rel_c,
            "c_value",
            "speedup_vs_c4",
            "PAR-Relaxed c Scaling (threads=8, file_stream)",
            "c (palette multiplier)",
            "Speedup vs c=4 (median throughput)",
            rel_c_png,
        )
        print(f"wrote_figure={rel_thread_png}")
        print(f"wrote_figure={exact_thread_png}")
        print(f"wrote_figure={rel_c_png}")

    print(f"wrote_table={rel_thread_csv}")
    print(f"wrote_table={exact_thread_csv}")
    print(f"wrote_table={rel_c_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
