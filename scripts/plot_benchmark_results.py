#!/usr/bin/env python3
"""Generate report-ready figures and summary tables from benchmark CSV files."""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Optional

import matplotlib.pyplot as plt
import pandas as pd


NUMERIC_COLUMNS = [
    "seed",
    "num_vertices",
    "delta_cap",
    "batch_size",
    "palette_multiplier",
    "palette_size",
    "max_rounds",
    "updates_requested",
    "updates_generated",
    "updates_applied",
    "updates_rejected",
    "accepted_ratio",
    "batches_generated",
    "batches_applied",
    "batch_accepted_ratio",
    "generation_attempts",
    "generator_same_color_attempts",
    "generator_same_color_chosen",
    "generator_same_color_fallbacks",
    "max_generation_attempts",
    "build_seconds",
    "update_seconds",
    "generation_seconds",
    "engine_apply_seconds",
    "graph_apply_seconds",
    "validate_seconds",
    "internal_validation_seconds",
    "throughput_updates_per_second",
    "max_degree_observed",
    "graph_validated",
    "coloring_validated",
    "vertices_touched_total",
    "total_rounds",
    "fallback_count",
    "repair_calls",
    "repair_rounds",
    "sequential_repair_calls",
    "sequential_repair_rounds",
    "neighbor_scans",
    "commits_total",
    "repair_seconds",
    "active_build_seconds",
    "recolor_calls",
    "recolored_vertices_total",
    "cascade_steps_total",
    "full_fallback_count",
    "level_conflict_choices",
    "active_vertices_total",
    "repair_rounds_total",
    "proposal_count",
    "commit_count",
    "unresolved_count",
    "sequential_fast_path_count",
    "max_active_size",
    "active_size_round_total",
    "neighbor_materializations",
    "direct_neighbor_scans",
    "parlay_threads",
]

ENGINE_ORDER = ["seq_baseline", "seq_exact", "par_relaxed", "par_exact"]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot benchmark results from parsed CSV.")
    parser.add_argument("--input", required=True, help="Input benchmark CSV path.")
    parser.add_argument("--outdir", default="plots/report", help="Output directory.")
    parser.add_argument("--prefix", default=None, help="Prefix for output filenames.")
    parser.add_argument("--format", choices=["png", "pdf"], default="png", help="Figure file format.")
    parser.add_argument("--show", action="store_true", help="Display plots interactively.")
    parser.add_argument(
        "--exclude-invalid",
        dest="exclude_invalid",
        action="store_true",
        default=True,
        help="Exclude rows where graph/color validation failed when columns exist (default).",
    )
    parser.add_argument(
        "--include-invalid",
        dest="exclude_invalid",
        action="store_false",
        help="Keep rows even if validation flags indicate failures.",
    )
    parser.add_argument("--plot-engine-comparison", action="store_true", help="Generate engine comparison plots.")
    parser.add_argument("--plot-c-scaling", action="store_true", help="Generate PAR-Relaxed c-scaling plots.")
    parser.add_argument("--plot-thread-scaling", action="store_true", help="Generate parallel thread-scaling plots.")
    parser.add_argument("--thread-num-vertices", type=float, default=None, help="Restrict thread plots to num_vertices.")
    parser.add_argument("--thread-delta-cap", type=float, default=None, help="Restrict thread plots to delta_cap.")
    parser.add_argument("--thread-batch-size", type=float, default=None, help="Restrict thread plots to batch_size.")
    parser.add_argument("--thread-updates", type=float, default=None, help="Restrict thread plots to updates_requested.")
    parser.add_argument(
        "--thread-metric",
        default="update_seconds",
        help="Timing metric for thread speedup plots (for example update_seconds, engine_apply_seconds, repair_seconds).",
    )
    return parser.parse_args()


def ensure_dirs(outdir: Path) -> tuple[Path, Path]:
    figures_dir = outdir / "figures"
    tables_dir = outdir / "tables"
    figures_dir.mkdir(parents=True, exist_ok=True)
    tables_dir.mkdir(parents=True, exist_ok=True)
    return figures_dir, tables_dir


def load_frame(input_path: Path) -> pd.DataFrame:
    df = pd.read_csv(input_path, dtype=str)
    for col in df.columns:
        df[col] = df[col].map(lambda v: v.strip() if isinstance(v, str) else v)
    if "parlay_threads" in df.columns:
        df["parlay_threads_raw"] = df["parlay_threads"]
    for col in NUMERIC_COLUMNS:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")
    if "batch_size" in df.columns:
        df["batch_size"] = df["batch_size"].fillna(-1)
    return df


def filtered_valid(df: pd.DataFrame, exclude_invalid: bool) -> pd.DataFrame:
    out = df.copy()
    if not exclude_invalid:
        return out
    if "graph_validated" in out.columns:
        out = out[out["graph_validated"].isna() | (out["graph_validated"] == 1)]
    if "coloring_validated" in out.columns:
        out = out[out["coloring_validated"].isna() | (out["coloring_validated"] == 1)]
    return out


def build_common_columns(df: pd.DataFrame) -> pd.DataFrame:
    out = df.copy()
    if "workload" in out.columns and "batch_size" in out.columns:
        batch_labels = out["batch_size"].map(lambda x: "?" if pd.isna(x) else str(int(x)))
        out["workload_batch"] = out["workload"].astype(str) + " (b=" + batch_labels + ")"
    else:
        out["workload_batch"] = out.get("workload", pd.Series(index=out.index, dtype=str)).astype(str)

    out["fallback_unified"] = pd.NA
    if "engine_name" in out.columns:
        if "full_fallback_count" in out.columns:
            seq_exact_mask = out["engine_name"] == "seq_exact"
            out.loc[seq_exact_mask, "fallback_unified"] = out.loc[seq_exact_mask, "full_fallback_count"]
        if "fallback_count" in out.columns:
            non_seq_mask = out["engine_name"].isin(["par_relaxed", "par_exact"])
            out.loc[non_seq_mask, "fallback_unified"] = out.loc[non_seq_mask, "fallback_count"]

    out["repair_effort_value"] = pd.NA
    out["repair_effort_label"] = pd.NA
    if "engine_name" in out.columns:
        seq_baseline_mask = out["engine_name"] == "seq_baseline"
        seq_exact_mask = out["engine_name"] == "seq_exact"
        par_relaxed_mask = out["engine_name"] == "par_relaxed"
        par_exact_mask = out["engine_name"] == "par_exact"

        if "vertices_touched_total" in out.columns:
            out.loc[seq_baseline_mask, "repair_effort_value"] = out.loc[seq_baseline_mask, "vertices_touched_total"]
        out.loc[seq_baseline_mask, "repair_effort_label"] = "vertices_touched_total"

        if "recolor_calls" in out.columns:
            out.loc[seq_exact_mask, "repair_effort_value"] = out.loc[seq_exact_mask, "recolor_calls"]
        out.loc[seq_exact_mask, "repair_effort_label"] = "recolor_calls"

        if "total_rounds" in out.columns:
            out.loc[par_relaxed_mask, "repair_effort_value"] = out.loc[par_relaxed_mask, "total_rounds"]
        out.loc[par_relaxed_mask, "repair_effort_label"] = "total_rounds"

        if "repair_rounds_total" in out.columns:
            out.loc[par_exact_mask, "repair_effort_value"] = out.loc[par_exact_mask, "repair_rounds_total"]
        out.loc[par_exact_mask, "repair_effort_label"] = "repair_rounds_total"

    for base_col in ["updates_applied", "updates_generated"]:
        if base_col not in out.columns:
            out[base_col] = pd.NA
    denom = out["updates_applied"].fillna(out["updates_generated"])
    vertices_col = out["vertices_touched_total"] if "vertices_touched_total" in out.columns else pd.Series(pd.NA, index=out.index)
    generator_col = (
        out["generator_same_color_attempts"] if "generator_same_color_attempts" in out.columns else pd.Series(pd.NA, index=out.index)
    )
    out["vertices_touched_per_update"] = vertices_col / denom
    out["generator_attempts_per_update"] = generator_col / out["updates_generated"]
    return out


def choose_small_engine_rows(df: pd.DataFrame) -> pd.DataFrame:
    out = df.copy()
    if "num_vertices" in out.columns and (out["num_vertices"] == 1000).any():
        out = out[out["num_vertices"] == 1000]
    if "delta_cap" in out.columns and (out["delta_cap"] == 16).any():
        out = out[out["delta_cap"] == 16]
    return out


def save_table(df: pd.DataFrame, tables_dir: Path, prefix: str, table_name: str) -> Path:
    path = tables_dir / f"{prefix}_{table_name}.csv"
    df.to_csv(path, index=False)
    return path


def save_figure(fig: plt.Figure, figures_dir: Path, prefix: str, name: str, fmt: str) -> Path:
    out_path = figures_dir / f"{prefix}_{name}.{fmt}"
    fig.tight_layout()
    fig.savefig(out_path, dpi=180 if fmt == "png" else None)
    plt.close(fig)
    return out_path


def write_manifest(paths: list[Path], outdir: Path, prefix: str) -> Path:
    rows = []
    for path in paths:
        if path.suffix.lower() in {".png", ".pdf"}:
            artifact_type = "figure"
        elif path.suffix.lower() == ".csv":
            artifact_type = "table"
        else:
            artifact_type = "other"
        rows.append(
            {
                "artifact_type": artifact_type,
                "path": str(path),
                "filename": path.name,
            }
        )
    manifest_path = outdir / "plot_manifest.csv"
    pd.DataFrame(rows).to_csv(manifest_path, index=False)
    return manifest_path


def grouped_stats(df: pd.DataFrame, group_cols: list[str], metric: str) -> pd.DataFrame:
    g = (
        df.groupby(group_cols, dropna=False)[metric]
        .agg(["mean", "std", "count"])
        .reset_index()
        .rename(columns={"mean": metric, "std": f"{metric}_std", "count": f"{metric}_n"})
    )
    return g


def plot_grouped_bar(
    stats: pd.DataFrame,
    x_col: str,
    series_col: str,
    y_col: str,
    title: str,
    y_label: str,
    log_scale: bool = False,
) -> plt.Figure:
    x_vals = list(dict.fromkeys(stats[x_col].astype(str).tolist()))
    s_vals = list(dict.fromkeys(stats[series_col].astype(str).tolist()))
    width = 0.8 / max(1, len(s_vals))
    x_positions = list(range(len(x_vals)))

    fig, ax = plt.subplots(figsize=(10, 5))
    for idx, s_name in enumerate(s_vals):
        means = []
        errs = []
        for x_name in x_vals:
            row = stats[(stats[x_col].astype(str) == x_name) & (stats[series_col].astype(str) == s_name)]
            if row.empty:
                means.append(float("nan"))
                errs.append(float("nan"))
                continue
            means.append(float(row.iloc[0][y_col]))
            n_col = f"{y_col}_n"
            std_col = f"{y_col}_std"
            if n_col in row.columns and std_col in row.columns and float(row.iloc[0][n_col]) > 1:
                errs.append(float(row.iloc[0][std_col]) if pd.notna(row.iloc[0][std_col]) else float("nan"))
            else:
                errs.append(float("nan"))

        offsets = [x + (idx - (len(s_vals) - 1) / 2.0) * width for x in x_positions]
        ax.bar(offsets, means, width=width, label=s_name, yerr=errs, capsize=3)

    ax.set_xticks(x_positions)
    ax.set_xticklabels(x_vals, rotation=25, ha="right")
    ax.set_title(title, fontsize=12)
    ax.set_ylabel(y_label)
    ax.legend(fontsize=9)
    ax.grid(axis="y", alpha=0.3)
    if log_scale:
        ax.set_yscale("log")
    return fig


def plot_lines(
    df: pd.DataFrame,
    x_col: str,
    y_col: str,
    series_col: str,
    title: str,
    y_label: str,
    x_label: str,
    ideal_line: bool = False,
) -> plt.Figure:
    fig, ax = plt.subplots(figsize=(10, 5))
    for series_name, g in df.groupby(series_col, dropna=False):
        g_sorted = g.sort_values(x_col)
        ax.plot(
            g_sorted[x_col],
            g_sorted[y_col],
            marker="o",
            linewidth=1.8,
            label=str(series_name),
        )
    if ideal_line:
        x_vals = sorted(v for v in df[x_col].dropna().unique() if v > 0)
        if 1 in x_vals:
            ax.plot(x_vals, x_vals, linestyle="--", linewidth=1.2, label="ideal linear speedup")
    ax.set_title(title, fontsize=12)
    ax.set_xlabel(x_label)
    ax.set_ylabel(y_label)
    ax.grid(alpha=0.3)
    ax.legend(fontsize=8)
    return fig


def make_validation_tables(df_raw: pd.DataFrame, tables_dir: Path, prefix: str) -> list[Path]:
    graph_col = "graph_validated" if "graph_validated" in df_raw.columns else None
    color_col = "coloring_validated" if "coloring_validated" in df_raw.columns else None

    graph_status = df_raw[graph_col] if graph_col else pd.Series(pd.NA, index=df_raw.index)
    color_status = df_raw[color_col] if color_col else pd.Series(pd.NA, index=df_raw.index)
    summary = (
        pd.DataFrame({"graph_validated": graph_status, "coloring_validated": color_status})
        .fillna("missing")
        .value_counts(dropna=False)
        .reset_index(name="row_count")
    )
    summary_path = save_table(summary, tables_dir, prefix, "validation_summary")

    invalid_mask = pd.Series(False, index=df_raw.index)
    if graph_col:
        invalid_mask = invalid_mask | (~df_raw[graph_col].isna() & (df_raw[graph_col] != 1))
    if color_col:
        invalid_mask = invalid_mask | (~df_raw[color_col].isna() & (df_raw[color_col] != 1))
    invalid_rows = df_raw[invalid_mask].copy()
    invalid_rows.insert(0, "row_index", invalid_rows.index)
    invalid_path = save_table(invalid_rows, tables_dir, prefix, "invalid_rows")
    return [summary_path, invalid_path]


def create_engine_comparison(df: pd.DataFrame, figures_dir: Path, tables_dir: Path, prefix: str, fmt: str) -> list[Path]:
    outputs: list[Path] = []
    required = {"engine_name", "workload", "batch_size"}
    if not required.issubset(df.columns):
        return outputs

    sub = df[
        df["engine_name"].isin(ENGINE_ORDER)
        & df["workload"].isin(["valid_insertions", "mixed_valid", "batch_valid", "conflict_heavy"])
    ].copy()
    sub = choose_small_engine_rows(sub)
    if sub.empty:
        return outputs

    workload_order = [
        "valid_insertions (b=1)",
        "mixed_valid (b=1)",
        "batch_valid (b=16)",
        "conflict_heavy (b=1)",
        "conflict_heavy (b=16)",
    ]
    sub["workload_batch"] = pd.Categorical(sub["workload_batch"], categories=workload_order, ordered=True)
    sub["engine_name"] = pd.Categorical(sub["engine_name"], categories=ENGINE_ORDER, ordered=True)

    metrics = [
        ("update_seconds", "engine_update_seconds_by_workload", "Mean update_seconds"),
        ("throughput_updates_per_second", "engine_throughput_by_workload", "Mean throughput_updates_per_second"),
        ("vertices_touched_total", "engine_vertices_touched_by_workload", "Mean vertices_touched_total"),
        ("repair_effort_value", "engine_repair_effort_by_workload", "Mean repair/recolor rounds or calls"),
    ]

    for metric, fig_name, y_label in metrics:
        if metric not in sub.columns:
            continue
        stats = grouped_stats(sub.dropna(subset=[metric]), ["workload_batch", "engine_name"], metric)
        if stats.empty:
            continue
        stats = stats.sort_values(["workload_batch", "engine_name"])
        outputs.append(save_table(stats, tables_dir, prefix, fig_name))
        log_scale = metric == "vertices_touched_total"
        if log_scale and stats[metric].min() > 0:
            ratio = float(stats[metric].max() / stats[metric].min())
            log_scale = ratio >= 100
        fig = plot_grouped_bar(
            stats,
            x_col="workload_batch",
            series_col="engine_name",
            y_col=metric,
            title=fig_name.replace("_", " ").title(),
            y_label=y_label,
            log_scale=log_scale,
        )
        outputs.append(save_figure(fig, figures_dir, prefix, fig_name, fmt))
    return outputs


def create_c_scaling(df: pd.DataFrame, figures_dir: Path, tables_dir: Path, prefix: str, fmt: str) -> list[Path]:
    outputs: list[Path] = []
    if "engine_name" not in df.columns or "palette_multiplier" not in df.columns:
        return outputs
    sub = df[(df["engine_name"] == "par_relaxed") & df["palette_multiplier"].notna()].copy()
    if sub.empty:
        return outputs
    if "parlay_threads_raw" in sub.columns:
        default_mask = sub["parlay_threads_raw"].astype(str).str.lower() == "default"
        if default_mask.any():
            sub = sub[default_mask]
    if "num_vertices" in sub.columns and sub["num_vertices"].nunique(dropna=True) > 1:
        sub = sub[sub["num_vertices"] == sub["num_vertices"].max()]
    if "delta_cap" in sub.columns and sub["delta_cap"].nunique(dropna=True) > 1:
        sub = sub[sub["delta_cap"] == sub["delta_cap"].max()]
    if sub.empty:
        return outputs
    sub["series"] = sub["workload_batch"]

    spec = [
        ("update_seconds", "par_relaxed_c_update_seconds", "Mean update_seconds"),
        ("vertices_touched_total", "par_relaxed_c_vertices_touched", "Mean vertices_touched_total"),
        ("total_rounds", "par_relaxed_c_repair_rounds", "Mean total_rounds"),
    ]
    for metric, fig_name, y_label in spec:
        if metric not in sub.columns:
            continue
        stats = grouped_stats(sub.dropna(subset=[metric]), ["series", "palette_multiplier"], metric)
        if stats.empty:
            continue
        outputs.append(save_table(stats, tables_dir, prefix, fig_name))
        fig = plot_lines(
            stats,
            x_col="palette_multiplier",
            y_col=metric,
            series_col="series",
            title=fig_name.replace("_", " ").title(),
            y_label=y_label,
            x_label="palette_multiplier (c)",
        )
        outputs.append(save_figure(fig, figures_dir, prefix, fig_name, fmt))

    conflict = sub[sub["workload"] == "conflict_heavy"].copy()
    if not conflict.empty and "generator_attempts_per_update" in conflict.columns:
        conflict["series"] = "conflict_heavy (b=" + conflict["batch_size"].map(lambda x: str(int(x))) + ")"
        stats = grouped_stats(
            conflict.dropna(subset=["generator_attempts_per_update"]),
            ["series", "palette_multiplier"],
            "generator_attempts_per_update",
        )
        if not stats.empty:
            fig_name = "par_relaxed_c_generator_difficulty"
            outputs.append(save_table(stats, tables_dir, prefix, fig_name))
            fig = plot_lines(
                stats,
                x_col="palette_multiplier",
                y_col="generator_attempts_per_update",
                series_col="series",
                title="Par Relaxed C Generator Difficulty",
                y_label="generator_same_color_attempts / updates_generated",
                x_label="palette_multiplier (c)",
            )
            outputs.append(save_figure(fig, figures_dir, prefix, fig_name, fmt))

    return outputs


def thread_series_label(row: pd.Series) -> str:
    batch_size = row.get("batch_size")
    num_vertices = row.get("num_vertices")
    delta_cap = row.get("delta_cap")
    palette_multiplier = row.get("palette_multiplier")
    parts = [
        str(row.get("engine_name", "")),
        str(row.get("workload", "")),
        f"b={int(batch_size)}" if pd.notna(batch_size) else "b=?",
        f"n={int(num_vertices)}" if pd.notna(num_vertices) else "n=?",
        f"Delta={int(delta_cap)}" if pd.notna(delta_cap) else "Delta=?",
    ]
    if pd.notna(palette_multiplier):
        parts.append(f"c={int(palette_multiplier)}")
    return " | ".join(parts)


def add_thread_series(df: pd.DataFrame) -> pd.DataFrame:
    out = df.copy()
    out["series"] = out.apply(thread_series_label, axis=1)
    return out


def save_exact_table(df: pd.DataFrame, tables_dir: Path, table_name: str) -> Path:
    path = tables_dir / f"{table_name}.csv"
    df.to_csv(path, index=False)
    return path


def create_thread_scaling(
    df: pd.DataFrame,
    figures_dir: Path,
    tables_dir: Path,
    prefix: str,
    fmt: str,
    metric_col: str = "update_seconds",
    thread_num_vertices: Optional[float] = None,
    thread_delta_cap: Optional[float] = None,
    thread_batch_size: Optional[float] = None,
    thread_updates: Optional[float] = None,
) -> list[Path]:
    outputs: list[Path] = []
    required = {"engine_name", "parlay_threads", metric_col, "workload", "batch_size"}
    if not required.issubset(df.columns):
        return outputs

    sub = df[df["engine_name"].isin(["par_relaxed", "par_exact"])].copy()
    if "parlay_threads_raw" in sub.columns:
        sub = sub[sub["parlay_threads_raw"].notna()]
        sub = sub[sub["parlay_threads_raw"].astype(str).str.lower() != "default"]
    sub = sub[sub["parlay_threads"].notna()].copy()
    sub = sub[(sub["parlay_threads"] > 0) & sub[metric_col].notna()]
    if sub["workload"].isin(["batch_valid", "conflict_heavy"]).any():
        sub = sub[sub["workload"].isin(["batch_valid", "conflict_heavy"])]
    if thread_num_vertices is not None and "num_vertices" in sub.columns:
        sub = sub[sub["num_vertices"] == thread_num_vertices]
    if thread_delta_cap is not None and "delta_cap" in sub.columns:
        sub = sub[sub["delta_cap"] == thread_delta_cap]
    if thread_batch_size is not None and "batch_size" in sub.columns:
        sub = sub[sub["batch_size"] == thread_batch_size]
    if thread_updates is not None and "updates_requested" in sub.columns:
        sub = sub[sub["updates_requested"] == thread_updates]
    if sub.empty:
        return outputs

    outputs.append(save_exact_table(sub, tables_dir, "thread_scaling_filtered_rows"))

    baseline_key_candidates = [
        "engine_name",
        "workload",
        "batch_size",
        "seed",
        "num_vertices",
        "delta_cap",
        "updates_requested",
        "updates_generated",
        "max_generation_attempts",
        "max_rounds",
        "palette_multiplier",
    ]
    baseline_key = [col for col in baseline_key_candidates if col in sub.columns]
    keyed = (
        sub.groupby(baseline_key + ["parlay_threads"], dropna=False)[metric_col]
        .agg(["mean", "count"])
        .reset_index()
        .rename(columns={"mean": metric_col, "count": "source_row_count"})
    )

    display_group_candidates = [
        "engine_name",
        "workload",
        "batch_size",
        "num_vertices",
        "delta_cap",
        "updates_requested",
        "updates_generated",
        "palette_multiplier",
        "max_rounds",
    ]
    display_group = [col for col in display_group_candidates if col in sub.columns]

    stats = (
        keyed.groupby(display_group + ["parlay_threads"], dropna=False)[metric_col]
        .agg(["mean", "std", "count"])
        .reset_index()
        .rename(columns={"mean": metric_col, "std": f"{metric_col}_std", "count": f"{metric_col}_n"})
    )
    stats = add_thread_series(stats)
    if not stats.empty:
        fig_name = f"parallel_thread_{metric_col}"
        outputs.append(save_table(stats, tables_dir, prefix, fig_name))
        fig = plot_lines(
            stats,
            x_col="parlay_threads",
            y_col=metric_col,
            series_col="series",
            title=f"Parallel Thread {metric_col}",
            y_label=f"Mean {metric_col}",
            x_label="parlay_threads",
        )
        outputs.append(save_figure(fig, figures_dir, prefix, fig_name, fmt))

    baseline = keyed[keyed["parlay_threads"] == 1][baseline_key + [metric_col]].rename(
        columns={metric_col: f"{metric_col}_t1"}
    )
    speed = keyed.merge(baseline, on=baseline_key, how="left")
    missing_baseline_count = int(speed[f"{metric_col}_t1"].isna().sum())
    if missing_baseline_count:
        print(f"warning=thread_speedup_missing_threads_1_baseline_count={missing_baseline_count}")
    speed["speedup"] = speed[f"{metric_col}_t1"] / speed[metric_col]
    speed = speed[speed["speedup"].notna()].copy()
    if not speed.empty:
        t1_bad = speed[(speed["parlay_threads"] == 1) & ((speed["speedup"] - 1.0).abs() > 1e-9)]
        if not t1_bad.empty:
            print(f"warning=thread_speedup_threads_1_not_one_count={len(t1_bad)}")
    suffix = "" if metric_col == "update_seconds" else f"_{metric_col}"
    outputs.append(save_exact_table(speed, tables_dir, f"thread_speedup_by_seed{suffix}"))

    speed_summary = (
        speed.groupby(display_group + ["parlay_threads"], dropna=False)["speedup"]
        .agg(["mean", "std", "count"])
        .reset_index()
        .rename(columns={"mean": "speedup", "std": "speedup_std", "count": "speedup_n"})
    )
    speed_summary = add_thread_series(speed_summary) if not speed_summary.empty else speed_summary
    outputs.append(save_exact_table(speed_summary, tables_dir, f"thread_speedup_summary{suffix}"))

    speed_stats = speed_summary
    if not speed_stats.empty:
        fig_name = f"parallel_thread_speedup{suffix}"
        outputs.append(save_table(speed_stats, tables_dir, prefix, fig_name))
        fig = plot_lines(
            speed_stats,
            x_col="parlay_threads",
            y_col="speedup",
            series_col="series",
            title=f"Parallel Thread Speedup ({metric_col})",
            y_label=f"Mean {metric_col} speedup (relative to 1 thread)",
            x_label="parlay_threads",
            ideal_line=True,
        )
        outputs.append(save_figure(fig, figures_dir, prefix, fig_name, fmt))

        eff = speed_stats.copy()
        eff["efficiency"] = eff["speedup"] / eff["parlay_threads"]
        fig_name = f"parallel_thread_efficiency{suffix}"
        outputs.append(save_table(eff, tables_dir, prefix, fig_name))
        fig = plot_lines(
            eff,
            x_col="parlay_threads",
            y_col="efficiency",
            series_col="series",
            title=f"Parallel Thread Efficiency ({metric_col})",
            y_label=f"Mean {metric_col} efficiency (speedup / threads)",
            x_label="parlay_threads",
        )
        outputs.append(save_figure(fig, figures_dir, prefix, fig_name, fmt))

    fast = sub.copy()
    fast["fast_path_fraction"] = pd.NA
    par_exact_mask = fast["engine_name"] == "par_exact"
    par_relaxed_mask = fast["engine_name"] == "par_relaxed"

    if "sequential_fast_path_count" in fast.columns and "repair_rounds_total" in fast.columns:
        denom = fast.loc[par_exact_mask, "repair_rounds_total"]
        fast.loc[par_exact_mask, "fast_path_fraction"] = fast.loc[par_exact_mask, "sequential_fast_path_count"] / denom
    if "sequential_repair_rounds" in fast.columns:
        if "repair_rounds" in fast.columns:
            denom_relaxed = fast.loc[par_relaxed_mask, "repair_rounds"]
        else:
            denom_relaxed = pd.Series(pd.NA, index=fast.index[par_relaxed_mask])
        if "total_rounds" in fast.columns:
            denom_relaxed = denom_relaxed.fillna(fast.loc[par_relaxed_mask, "total_rounds"])
        fast.loc[par_relaxed_mask, "fast_path_fraction"] = (
            fast.loc[par_relaxed_mask, "sequential_repair_rounds"] / denom_relaxed
        )
    fast = fast[fast["fast_path_fraction"].notna()].copy()
    if not fast.empty:
        fast_stats = (
            fast.groupby(display_group + ["parlay_threads"], dropna=False)["fast_path_fraction"]
            .agg(["mean", "std", "count"])
            .reset_index()
            .rename(
                columns={
                    "mean": "fast_path_fraction",
                    "std": "fast_path_fraction_std",
                    "count": "fast_path_fraction_n",
                }
            )
        )
        fast_stats = add_thread_series(fast_stats)
        fig_name = "parallel_thread_sequential_fast_path"
        outputs.append(save_table(fast_stats, tables_dir, prefix, fig_name))
        fig = plot_lines(
            fast_stats,
            x_col="parlay_threads",
            y_col="fast_path_fraction",
            series_col="series",
            title="Parallel Thread Sequential Fast Path Fraction",
            y_label="Mean fast-path fraction",
            x_label="parlay_threads",
        )
        outputs.append(save_figure(fig, figures_dir, prefix, fig_name, fmt))
        classification = fast.copy()
        generation = classification.get("generation_seconds", pd.Series(0.0, index=classification.index)).fillna(0.0)
        engine_apply = classification.get("engine_apply_seconds", pd.Series(0.0, index=classification.index)).fillna(0.0)
        graph_apply = classification.get("graph_apply_seconds", pd.Series(0.0, index=classification.index)).fillna(0.0)
        internal_validation = classification.get(
            "internal_validation_seconds", pd.Series(0.0, index=classification.index)
        ).fillna(0.0)
        repair = classification.get("repair_seconds", pd.Series(0.0, index=classification.index)).fillna(0.0)
        total = generation + engine_apply
        classification["generation_fraction"] = generation / total.where(total > 0)
        classification["graph_apply_fraction"] = graph_apply / engine_apply.where(engine_apply > 0)
        classification["internal_validation_fraction"] = internal_validation / engine_apply.where(engine_apply > 0)
        classification["repair_fraction"] = repair / engine_apply.where(engine_apply > 0)
        classification["parallelism_class"] = "parallel_repair_active"
        classification.loc[classification["fast_path_fraction"] >= 0.95, "parallelism_class"] = "too_little_parallel_work"
        classification.loc[classification["generation_fraction"] >= 0.5, "parallelism_class"] = "dominated_by_generation"
        classification.loc[
            (classification["graph_apply_fraction"] + classification["internal_validation_fraction"]) >= 0.5,
            "parallelism_class",
        ] = "dominated_by_graph_or_validation"
        classification_cols = [
            col
            for col in display_group
            + [
                "seed",
                "parlay_threads",
                "fast_path_fraction",
                "generation_fraction",
                "graph_apply_fraction",
                "internal_validation_fraction",
                "repair_fraction",
                "max_active_size",
                "active_size_round_total",
                "parallelism_class",
            ]
            if col in classification.columns
        ]
        outputs.append(save_exact_table(classification[classification_cols], tables_dir, "thread_scaling_classification"))
    return outputs


def create_benchmark_summary(df: pd.DataFrame, tables_dir: Path, prefix: str) -> Path:
    group_cols = ["engine_name", "workload", "batch_size", "palette_multiplier", "parlay_threads"]
    existing_group_cols = [c for c in group_cols if c in df.columns]
    metrics = [
        "update_seconds",
        "generation_seconds",
        "engine_apply_seconds",
        "graph_apply_seconds",
        "repair_seconds",
        "active_build_seconds",
        "internal_validation_seconds",
        "throughput_updates_per_second",
        "vertices_touched_total",
        "accepted_ratio",
        "fallback_count",
        "full_fallback_count",
        "total_rounds",
        "repair_rounds_total",
        "recolor_calls",
    ]
    existing_metrics = [m for m in metrics if m in df.columns]
    if not existing_group_cols or not existing_metrics:
        empty = pd.DataFrame(columns=existing_group_cols + existing_metrics)
        return save_table(empty, tables_dir, prefix, "benchmark_summary_by_group")

    summary = (
        df.groupby(existing_group_cols, dropna=False)[existing_metrics]
        .agg(["mean", "std", "count"])
        .reset_index()
    )
    summary.columns = [
        "_".join([str(p) for p in col if p]).rstrip("_") if isinstance(col, tuple) else str(col)
        for col in summary.columns
    ]
    return save_table(summary, tables_dir, prefix, "benchmark_summary_by_group")


def main() -> None:
    args = parse_args()
    input_path = Path(args.input)
    outdir = Path(args.outdir)
    prefix = args.prefix or input_path.stem
    figures_dir, tables_dir = ensure_dirs(outdir)

    df_raw = load_frame(input_path)
    validation_outputs = make_validation_tables(df_raw, tables_dir, prefix)
    df = filtered_valid(df_raw, exclude_invalid=args.exclude_invalid)
    df = build_common_columns(df)

    any_plot_flag = args.plot_engine_comparison or args.plot_c_scaling or args.plot_thread_scaling
    plot_engine_comparison = args.plot_engine_comparison or not any_plot_flag
    plot_c_scaling = args.plot_c_scaling or not any_plot_flag
    plot_thread_scaling = args.plot_thread_scaling or not any_plot_flag

    generated: list[Path] = []
    generated.extend(validation_outputs)
    generated.append(create_benchmark_summary(df, tables_dir, prefix))
    if plot_engine_comparison:
        generated.extend(create_engine_comparison(df, figures_dir, tables_dir, prefix, args.format))
    if plot_c_scaling:
        generated.extend(create_c_scaling(df, figures_dir, tables_dir, prefix, args.format))
    if plot_thread_scaling:
        generated.extend(
            create_thread_scaling(
                df,
                figures_dir,
                tables_dir,
                prefix,
                args.format,
                metric_col=args.thread_metric,
                thread_num_vertices=args.thread_num_vertices,
                thread_delta_cap=args.thread_delta_cap,
                thread_batch_size=args.thread_batch_size,
                thread_updates=args.thread_updates,
            )
        )
    manifest_path = write_manifest(generated, outdir, prefix)
    generated.append(manifest_path)

    print(f"input_rows={len(df_raw)}")
    print(f"rows_after_filter={len(df)}")
    print(f"output_dir={outdir}")
    for path in generated:
        print(f"wrote={path}")

    if args.show:
        plt.show()


if __name__ == "__main__":
    main()
