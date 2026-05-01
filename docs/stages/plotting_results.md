# Plotting Benchmark Results

## Script

Use `scripts/plot_benchmark_results.py` to generate report-ready figures and summary tables from parsed benchmark CSV files (for example, output from `scripts/parse_bench_kv.awk`).

## Input Format

Expected input is a CSV containing key/value benchmark rows with columns such as:

- `engine_name`, `workload`, `batch_size`, `seed`
- `update_seconds`, `throughput_updates_per_second`
- `vertices_touched_total`
- `palette_multiplier` (for `par_relaxed`)
- `parlay_threads` (numeric for thread scaling)
- validation flags: `graph_validated`, `coloring_validated`

Engine-specific fields may be missing or empty; the script coerces numeric columns with `errors="coerce"` and treats missing values as `NaN`.

## Usage

From repo root:

```bash
python3 scripts/plot_benchmark_results.py --input logs/scaling/scaling_suite.csv --outdir plots/report
```

PDF output:

```bash
python3 scripts/plot_benchmark_results.py --input logs/scaling/scaling_suite.csv --outdir plots/report --format pdf
```

Keep invalid rows:

```bash
python3 scripts/plot_benchmark_results.py --input logs/scaling/scaling_suite.csv --outdir plots/report --include-invalid
```

## Outputs

The script writes:

- `plots/report/figures/<prefix>_*.png` (or `.pdf`)
- `plots/report/tables/<prefix>_*.csv`

This includes:

- engine comparison plots,
- `par_relaxed` c-scaling plots,
- thread scaling/speedup/efficiency plots,
- sequential-fast-path diagnostics,
- `validation_summary` and grouped benchmark summary tables.
