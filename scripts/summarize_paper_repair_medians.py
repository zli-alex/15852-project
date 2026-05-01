#!/usr/bin/env python3
"""Summarize repeated paper-repair benchmark CSVs with median metrics."""

import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path


METRICS = [
    "engine_apply_seconds",
    "repair_seconds",
    "graph_apply_seconds",
    "generation_seconds",
    "validate_seconds",
    "fallback_count",
    "unresolved_count",
    "direct_neighbor_scans",
    "level_ge_neighbor_scans",
    "level_le_neighbor_scans",
    "token_safe_commits",
    "token_active_conflict_rejections",
    "token_lower_equal_conflict_rejections",
    "token_unique_higher_moves",
    "token_multi_higher_conflicts",
]


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_path", type=Path, help="Parsed benchmark CSV to summarize.")
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Optional output CSV path. Defaults to stdout.",
    )
    return parser.parse_args()


def to_float(value):
    if value == "":
        return None
    try:
        return float(value)
    except ValueError:
        return None


def main():
    args = parse_args()
    groups = defaultdict(list)
    with args.csv_path.open(newline="") as fh:
        for row in csv.DictReader(fh):
            if row.get("engine_name") != "par_exact":
                continue
            key = (
                row.get("git_commit", ""),
                row.get("par_exact_token_repair", ""),
                row.get("batch_size", ""),
                row.get("parlay_threads", ""),
            )
            groups[key].append(row)

    fieldnames = ["git_commit", "par_exact_token_repair", "batch_size", "parlay_threads", "samples"]
    fieldnames.extend(f"median_{metric}" for metric in METRICS)

    rows = []

    def sort_key(item):
        key = item[0]
        return (key[0], key[1], int(key[2]), int(key[3]))

    for key, samples in sorted(groups.items(), key=sort_key):
        out = {
            "git_commit": key[0],
            "par_exact_token_repair": key[1],
            "batch_size": key[2],
            "parlay_threads": key[3],
            "samples": str(len(samples)),
        }
        for metric in METRICS:
            values = []
            for row in samples:
                value = to_float(row.get(metric, ""))
                if value is not None:
                    values.append(value)
            out[f"median_{metric}"] = f"{statistics.median(values):.9g}" if values else ""
        rows.append(out)

    if args.output is None:
        writer = csv.DictWriter(__import__("sys").stdout, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", newline="") as fh:
            writer = csv.DictWriter(fh, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
