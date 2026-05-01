#!/usr/bin/env python3
"""Prepare static edge-list datasets as deterministic dynamic update streams."""

from __future__ import annotations

import argparse
import gzip
import json
import random
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


RawEdge = Tuple[str, str]
MappedEdge = Tuple[int, int]


DELIMITER_RE = re.compile(r"[\s,]+")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Convert a plain-text or gzipped edge list into initial_edges.txt, "
            "updates.txt, and metadata.json for dynamic coloring benchmarks."
        )
    )
    parser.add_argument("--input", required=True, help="Input edge-list file (.txt or .txt.gz).")
    parser.add_argument("--output-dir", default="data/prepared", help="Root output directory.")
    parser.add_argument("--name", required=True, help="Dataset name used in the output path.")
    parser.add_argument("--delta-cap", type=positive_int, default=64, help="Maximum degree allowed.")
    parser.add_argument(
        "--initial-edges",
        type=nonnegative_int,
        default=0,
        help="Number of accepted edges to place in initial_edges.txt.",
    )
    parser.add_argument(
        "--updates",
        type=nonnegative_int,
        default=10000,
        help="Target number of update records to write.",
    )
    parser.add_argument("--batch-size", type=positive_int, default=64, help="Benchmark batch size metadata.")
    parser.add_argument("--seed", type=int, default=1, help="Deterministic shuffle seed.")
    parser.add_argument(
        "--mode",
        choices=("insertion_only", "mixed"),
        default="insertion_only",
        help="Update stream construction policy.",
    )
    parser.add_argument(
        "--undirected",
        dest="undirected",
        action="store_true",
        default=True,
        help="Canonicalize edges as undirected pairs (default).",
    )
    parser.add_argument(
        "--directed",
        dest="undirected",
        action="store_false",
        help="Keep edge orientation for duplicate detection and output.",
    )
    parser.add_argument(
        "--max-raw-edges",
        type=positive_int,
        default=None,
        help="Stop after this many parseable raw edge records.",
    )
    parser.add_argument(
        "--largest-component",
        action="store_true",
        help="Accepted for CLI compatibility; component filtering is not implemented yet.",
    )
    parser.add_argument(
        "--shuffle",
        dest="shuffle",
        action="store_true",
        default=True,
        help="Shuffle candidate edges deterministically before applying the degree cap (default).",
    )
    parser.add_argument(
        "--no-shuffle",
        dest="shuffle",
        action="store_false",
        help="Preserve input order when applying the degree cap.",
    )
    return parser.parse_args()


def positive_int(text: str) -> int:
    value = int(text)
    if value <= 0:
        raise argparse.ArgumentTypeError("expected a positive integer")
    return value


def nonnegative_int(text: str) -> int:
    value = int(text)
    if value < 0:
        raise argparse.ArgumentTypeError("expected a nonnegative integer")
    return value


def open_text(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", encoding="utf-8", errors="replace")
    return path.open("rt", encoding="utf-8", errors="replace")


def raw_edge_key(u: str, v: str, undirected: bool) -> RawEdge:
    if not undirected:
        return (u, v)
    return (u, v) if u <= v else (v, u)


def read_unique_edges(path: Path, undirected: bool, max_raw_edges: Optional[int]) -> Tuple[List[RawEdge], Dict[str, int]]:
    counters = {
        "original_raw_edges_seen": 0,
        "skipped_self_loops": 0,
        "skipped_duplicates": 0,
        "skipped_malformed": 0,
    }
    seen = set()
    edges: List[RawEdge] = []

    with open_text(path) as handle:
        for line in handle:
            stripped = line.strip()
            if not stripped or stripped.startswith("#") or stripped.startswith("%"):
                continue

            parts = [part for part in DELIMITER_RE.split(stripped) if part]
            if len(parts) < 2:
                counters["skipped_malformed"] += 1
                continue

            counters["original_raw_edges_seen"] += 1
            u, v = parts[0], parts[1]
            if u == v:
                counters["skipped_self_loops"] += 1
            else:
                edge = raw_edge_key(u, v, undirected)
                if edge in seen:
                    counters["skipped_duplicates"] += 1
                else:
                    seen.add(edge)
                    edges.append(edge)

            if max_raw_edges is not None and counters["original_raw_edges_seen"] >= max_raw_edges:
                break

    return edges, counters


def select_degree_capped_edges(edges: Iterable[RawEdge], delta_cap: int) -> Tuple[List[RawEdge], int]:
    degrees: Dict[str, int] = defaultdict(int)
    accepted: List[RawEdge] = []
    skipped_degree_cap = 0

    for u, v in edges:
        if degrees[u] >= delta_cap or degrees[v] >= delta_cap:
            skipped_degree_cap += 1
            continue
        degrees[u] += 1
        degrees[v] += 1
        accepted.append((u, v))

    return accepted, skipped_degree_cap


def max_degree(edges: Iterable[RawEdge]) -> int:
    degrees: Dict[str, int] = defaultdict(int)
    for u, v in edges:
        degrees[u] += 1
        degrees[v] += 1
    return max(degrees.values(), default=0)


def remap_edges(edges: Sequence[RawEdge]) -> Tuple[List[MappedEdge], Dict[str, int]]:
    nodes = sorted({node for edge in edges for node in edge})
    mapping = {node: i for i, node in enumerate(nodes)}
    return [(mapping[u], mapping[v]) for u, v in edges], mapping


def build_stream(
    accepted_edges: Sequence[MappedEdge], initial_edge_count: int, update_count: int, mode: str
) -> Tuple[List[MappedEdge], List[str]]:
    initial_edges = list(accepted_edges[:initial_edge_count])
    remaining = list(accepted_edges[initial_edge_count:])

    if mode == "insertion_only":
        updates = [f"I {u} {v}" for u, v in remaining[:update_count]]
        return initial_edges, updates

    insert_target = min(len(remaining), (update_count + 1) // 2)
    inserted = remaining[:insert_target]
    delete_target = min(update_count - insert_target, len(inserted))

    updates = [f"I {u} {v}" for u, v in inserted]
    updates.extend(f"D {u} {v}" for u, v in inserted[:delete_target])
    return initial_edges, updates


def stream_edge_count(accepted_edge_count: int, initial_edge_count: int, update_count: int, mode: str) -> int:
    initial_count = min(accepted_edge_count, initial_edge_count)
    remaining_count = max(0, accepted_edge_count - initial_count)
    if mode == "insertion_only":
        return initial_count + min(remaining_count, update_count)
    return initial_count + min(remaining_count, (update_count + 1) // 2)


def write_lines(path: Path, lines: Iterable[str]) -> None:
    path.write_text("".join(f"{line}\n" for line in lines), encoding="utf-8")


def main() -> int:
    args = parse_args()
    input_path = Path(args.input)
    if not input_path.exists():
        print(f"error: input file does not exist: {input_path}", file=sys.stderr)
        return 2

    if args.largest_component:
        print(
            "warning: --largest-component is accepted but not implemented; using all filtered edges",
            file=sys.stderr,
        )

    unique_edges, counters = read_unique_edges(input_path, args.undirected, args.max_raw_edges)
    candidates = list(unique_edges)
    if args.shuffle:
        random.Random(args.seed).shuffle(candidates)

    accepted_raw_edges, skipped_degree_cap = select_degree_capped_edges(candidates, args.delta_cap)
    needed_edges = stream_edge_count(
        len(accepted_raw_edges), args.initial_edges, args.updates, args.mode
    )
    stream_raw_edges = accepted_raw_edges[:needed_edges]
    mapped_edges, mapping = remap_edges(stream_raw_edges)
    initial_edges, updates = build_stream(mapped_edges, args.initial_edges, args.updates, args.mode)

    prepared_dir = Path(args.output_dir) / args.name / f"seed_{args.seed}"
    prepared_dir.mkdir(parents=True, exist_ok=True)

    write_lines(prepared_dir / "initial_edges.txt", (f"{u} {v}" for u, v in initial_edges))
    write_lines(prepared_dir / "updates.txt", updates)

    metadata = {
        "dataset_name": args.name,
        "source_path": str(input_path),
        "seed": args.seed,
        "delta_cap": args.delta_cap,
        "original_raw_edges_seen": counters["original_raw_edges_seen"],
        "unique_edges_after_filtering": len(unique_edges),
        "remapped_num_vertices": len(mapping),
        "initial_edges_written": len(initial_edges),
        "updates_written": len(updates),
        "max_degree_observed": max_degree(stream_raw_edges),
        "skipped_self_loops": counters["skipped_self_loops"],
        "skipped_duplicates": counters["skipped_duplicates"],
        "skipped_degree_cap": skipped_degree_cap,
        "batch_size": args.batch_size,
        "mode": args.mode,
        "undirected": args.undirected,
        "shuffle": args.shuffle,
        "max_raw_edges": args.max_raw_edges,
        "largest_component_requested": args.largest_component,
        "skipped_malformed": counters["skipped_malformed"],
        "prepared_dir": str(prepared_dir),
    }
    (prepared_dir / "metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    print(f"prepared_dir={prepared_dir}")
    print(f"initial_edges={prepared_dir / 'initial_edges.txt'}")
    print(f"updates={prepared_dir / 'updates.txt'}")
    print(f"metadata={prepared_dir / 'metadata.json'}")
    print(f"initial_edges_written={len(initial_edges)}")
    print(f"updates_written={len(updates)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
