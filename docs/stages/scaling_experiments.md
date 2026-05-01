# Scaling Experiments

## Goal

This stage adds baseline synthetic scaling probes for the current implemented benchmark workloads. The scripts are intended to produce repeatable Linux-cluster logs for early `PAR-Relaxed` scaling checks, not final performance claims.

Implemented workload coverage:

- `batch_valid`
- `valid_insertions`
- `mixed_valid`
- `random_attempts` as the rejection-stress control

The initial scripts focus on `batch_valid` because it gives accepted insertion batches for `batch_size=16`, avoiding the rejection-heavy behavior of `random_attempts`.

## Scripts

### `scripts/run_scaling_suite.sh`

Runs the current comprehensive synthetic scaling suite and writes one combined log under `logs/scaling/`:

- configures and builds Release,
- runs full CTest once before benchmarking,
- logs `date`, `host`, `git_commit`, `root_dir`, `log_path`, and all suite config values,
- runs a four-engine comparison on moderate workloads,
- runs `par_relaxed` c-scaling on larger workloads,
- runs thread-count scaling for `par_relaxed` and `par_exact`.

Run a small first check:

```bash
RUN_C_SCALING=0 RUN_THREAD_SCALING=0 SEEDS="1" bash scripts/run_scaling_suite.sh
```

Run c-scaling only:

```bash
RUN_ENGINE_COMPARISON=0 RUN_THREAD_SCALING=0 SEEDS="1 2 3" C_VALUES="2 4 8 16" bash scripts/run_scaling_suite.sh
```

Run thread-scaling only:

```bash
RUN_ENGINE_COMPARISON=0 RUN_C_SCALING=0 SEEDS="1 2 3" THREAD_VALUES="1 2 4 8" bash scripts/run_scaling_suite.sh
```

Convert logs to CSV:

```bash
awk -f scripts/parse_bench_kv.awk logs/scaling/scaling_suite_*.log > logs/scaling/scaling_suite.csv
```

Interpretation caveats:

- For `conflict_heavy`, larger `c` makes same-color endpoints rarer, so `generation_attempts` and `generator_same_color_attempts` are part of the result.
- If `sequential_fast_path_count` is close to repair rounds/calls, thread speedup may be limited because active repair sets are small.

### `scripts/run_synthetic_c_sweep.sh`

Runs a palette multiplier sweep for `par_relaxed`:

- configures and builds Release,
- runs full CTest before benchmarking,
- writes logs under `logs/scaling/`,
- logs `date`, `host`, and `git_commit`,
- sets `PARLAY_NUM_THREADS=1`,
- runs `batch_valid` with:
  - `--vertices 10000`
  - `--updates 10000`
  - `--delta-cap 32`
  - `--batch-size 16`
  - `--max-rounds 4`
  - `--diagnostics 1`
- sweeps `c in {2,4,8,16}`,
- runs seeds `1 2 3` by default.

Run:

```bash
./scripts/run_synthetic_c_sweep.sh
```

For a smaller smoke run:

```bash
VERTICES=1000 UPDATES=1000 SEEDS=1 C_VALUES="2 4" ./scripts/run_synthetic_c_sweep.sh
```

### `scripts/run_synthetic_thread_sweep.sh`

Runs a Parlay thread-count sweep for `par_relaxed`:

- configures and builds Release,
- runs full CTest before benchmarking,
- writes logs under `logs/scaling/`,
- logs `date`, `host`, and `git_commit`,
- runs `batch_valid` with:
  - `--vertices 10000`
  - `--updates 10000`
  - `--delta-cap 32`
  - `--batch-size 16`
  - `--c 4`
  - `--max-rounds 4`
  - `--diagnostics 1`
- sweeps `PARLAY_NUM_THREADS in {1,2,4,8,16}`.

Run:

```bash
./scripts/run_synthetic_thread_sweep.sh
```

For a smaller smoke run:

```bash
VERTICES=1000 UPDATES=1000 THREAD_VALUES="1 2" ./scripts/run_synthetic_thread_sweep.sh
```

Thread scaling may be masked if `sequential_repair_rounds / repair_rounds` is near `1`, because tiny repair sets intentionally use the sequential repair fast path. This occurred in the recorded conflict-heavy batch c-sweep result.

## Parser

`scripts/parse_bench_kv.awk` converts key/value benchmark logs into CSV.

Example:

```bash
awk -f scripts/parse_bench_kv.awk logs/scaling/synthetic_c_sweep_*.log > logs/scaling/c_sweep.csv
awk -f scripts/parse_bench_kv.awk logs/scaling/synthetic_thread_sweep_*.log > logs/scaling/thread_sweep.csv
```

CSV columns include:

- `host`
- `date`
- `git_commit`
- `parlay_threads`
- `engine_name`
- `workload`
- `seed`
- `num_vertices`
- `delta_cap`
- `batch_size`
- `palette_multiplier`
- `updates_generated`
- `updates_applied`
- `accepted_ratio`
- `update_seconds`
- `throughput_updates_per_second`
- `repair_seconds`
- `repair_calls`
- `sequential_repair_calls`
- `sequential_repair_rounds`
- `total_rounds`
- `fallback_count`
- `vertices_touched_total`
- `neighbor_scans`
- `commits_total`
- `graph_validated`
- `coloring_validated`

## Conflict-Heavy Batch C-Sweep Result

Recorded conflict-heavy batch c-sweep configuration:

- `vertices=10000`
- `updates=6800`
- `delta_cap=32`
- `batch_size=16`
- `seeds=1,2,3`
- `c in {2,4,8,16}`
- `PARLAY_NUM_THREADS=1`
- `max_generation_attempts=5000000`

Validation outcome across all listed `c`/seed runs:

- `updates_generated=6800`
- `accepted_ratio=1`
- `batch_accepted_ratio=1`
- `graph_validated=1`
- `coloring_validated=1`
- `fallback_count=0`

Main finding:

- Larger `c` sharply increases `generator_same_color_attempts`, because same-color endpoint pairs are rarer.
- Repair-side metrics remain stable across `c`:
  - `repair_calls` around `425`,
  - `total_rounds` around `425-427`,
  - `vertices_touched_total` around `13.5k`.
- `update_seconds` changes only mildly with `c`.

Caveat:

- This workload is intentionally adversarial (it seeks same-color insertions), so it does not represent the natural conflict-probability reduction expected from larger `c`.
- All repair rounds still use the sequential fast path, so this experiment is not a thread-scaling result.

Conclusion:

- The current c-sweep is useful for adversarial conflict-heavy behavior and generator difficulty.
- Meaningful thread scaling still requires larger active sets or a workload/setting mix that triggers parallel repair rounds.

## How to Interpret Results

### C sweep

The c sweep asks whether the relaxed palette multiplier changes runtime or repair behavior on accepted synthetic batches.

Useful fields:

- `palette_multiplier`
- `update_seconds`
- `throughput_updates_per_second`
- `repair_seconds`
- `repair_calls`
- `total_rounds`
- `fallback_count`
- `commits_total`
- `accepted_ratio`

Interpretation caveat:

- `batch_valid` c-scaling can be too smooth because it does not target same-color conflicts.
- `conflict_heavy` c-sweeps are useful stress tests, but because they are adversarial they emphasize generator difficulty more than natural conflict-rate changes.

### Thread sweep

The thread sweep asks whether the current accepted-batch workload benefits from more Parlay workers.

Useful fields:

- `parlay_threads`
- `update_seconds`
- `throughput_updates_per_second`
- `repair_seconds`
- `repair_rounds`
- `sequential_repair_rounds`
- `repair_calls`

Interpretation caveat:

- If `sequential_repair_rounds / repair_rounds` is close to `1`, thread scaling is expected to be weak or absent because most repair work bypasses `parallel_for`.
- Meaningful thread scaling may require workloads where `sequential_repair_rounds / repair_rounds` is below `1`, which likely means larger active repair sets or future `conflict_heavy` workloads.

## Baseline Synthetic Scope

These scripts are a baseline synthetic scaling probe. They are useful for:

- verifying scripts and log parsing,
- checking accepted-batch throughput,
- catching major regressions,
- recording deterministic seed-controlled runs.

They are not a replacement for:

- conflict-heavy repair scaling,
- real graph datasets,
- accepted-batch-heavy mixed insert/delete workloads,
- paper-quality final experiments.

## Validation Policy

Each scaling script runs full CTest before benchmarks. A scaling log should only be used for comparisons if:

- CTest passes,
- `graph_validated=1`,
- `coloring_validated=1` for coloring engines,
- `accepted_ratio` is reported and understood,
- `git_commit`, `host`, `date`, and `parlay_threads` are present.

Parlay external-header warnings may appear during build and are not currently blocking.
