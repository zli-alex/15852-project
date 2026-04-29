# PAR-Relaxed Performance Investigation Plan

## 1. Goal and non-goals

### Goal

Investigate why `par_relaxed` shows high `update_seconds` for small smoke benchmarks, especially `batch_size=4`, after Step 6 phase-separated repair rounds.

The investigation should identify where time is spent, which inputs trigger the overhead, and whether the next implementation step should target instrumentation, benchmark coverage, or optimization.

### Non-goals

- Do not optimize `ParRelaxedEngine` yet.
- Do not change correctness semantics, repair behavior, rejection behavior, or fallback behavior.
- Do not change C++ files, tests, benchmark logic, or smoke scripts as part of this planning step.
- Do not treat small-graph smoke slowness as a correctness failure.
- Do not require macOS measurements; Linux cluster runs are the source of truth.
- Do not require `perf` on login nodes or environments where it is unavailable.

## 2. Current observed benchmark symptoms

Current validated behavior:

- `scripts/run_par_relaxed_smoke.sh` completes with `run_complete=1` on Linux.
- Full CTest passes: `100% tests passed, 0 tests failed out of 10`.
- `par_relaxed` validates graph invariants and coloring invariants.
- `total_rounds`, `fallback_count`, and `vertices_touched_total` are meaningful Step 6 metrics.

Observed performance symptom:

- `par_relaxed` with `batch_size=1` touches far fewer vertices than `seq_baseline` and performs reasonably.
- `par_relaxed` with `batch_size=4` validates correctly but can show high `update_seconds` on small smoke graphs.
- The issue appears to be overhead in the repair machinery or benchmark workload shape, not invalid coloring or graph corruption.
- Small graph settings such as `--vertices 32 --updates 100 --delta-cap 8` may amplify fixed parallel/runtime/allocation costs.

Important current benchmark limitation:

- Existing benchmark output reports aggregate update time and a few aggregate repair counters.
- It does not yet split time across active-set construction, proposal, safety, commit, unresolved recomputation, validation, rejected batch handling, or graph mutation.

## 3. Hypotheses for overhead

Primary hypotheses to test:

- Parlay `parallel_for` overhead dominates when active sets are tiny.
- `attempt_parallel_repair` allocates too many temporary vectors/sequences per accepted batch or per repair round.
- `graph_.neighbors(v)` returns copied `parlay::sequence` values repeatedly, causing hidden allocation/copy costs during expansion, conflict collection, proposal, safety, validation, and greedy fallback.
- Active-set expansion includes too many vertices compared with the actual conflicted set.
- Conflict collection scans too much neighborhood, especially after every round when recomputing unresolved vertices.
- Batch mode calls repair machinery even when no actual conflicts exist after accepted inserted edges.
- Timing includes many rejected batches or repair attempts with little useful repair work.
- Small graph smoke benchmarks are not representative of real parallel performance and may mostly measure scheduling, allocation, validation, and benchmark harness overhead.

Implementation-specific places to inspect later:

- `apply_batch()` builds endpoints for all inserted updates, expands with neighbors, and calls `attempt_parallel_repair()` whenever that expanded set is non-empty.
- `attempt_parallel_repair()` builds `active_index`, `proposed`, and `safe` every round, uses multiple `parallel_for` phases, then expands and rescans unresolved conflicts.
- `expand_with_neighbors()` and `collect_conflicted_vertices_from_candidates()` both call `graph_.neighbors(v)` repeatedly.
- Proposal and safety phases both call `graph_.neighbors(v)` for each active vertex.
- `validate_coloring_or_throw()` runs after accepted updates/batches and may matter on tiny workloads.

## 4. Metrics to collect

Keep existing benchmark metrics:

- `update_seconds`
- `validate_seconds`
- `updates_applied`
- `updates_rejected`
- `vertices_touched_total`
- `total_rounds`
- `fallback_count`
- `max_degree_observed`

First instrumentation patch, kept deliberately coarse:

- `repair_calls`
- `repair_rounds`
- `active_vertices_initial_total`
- `active_vertices_expanded_total`
- `conflicted_vertices_initial_total`
- `neighbor_scans`
- `commits_total`
- `repair_seconds`
- `active_build_seconds`

Possible later instrumentation, only if coarse metrics do not explain the issue:

- proposal phase
- safety phase
- commit phase
- unresolved recomputation
- validation

Derived metrics to compute from logs:

- applied batches versus rejected batches
- repair calls per accepted batch
- rounds per repair call
- commits per repair call
- neighbor scans per applied update
- touched vertices per applied update
- `update_seconds / updates_requested`
- `update_seconds / updates_applied`
- `repair_seconds / update_seconds`
- `active_build_seconds / update_seconds`

## 5. Minimal instrumentation design

Add instrumentation only after this investigation plan is accepted.

Design principles:

- Prefer an opt-in compile-time or runtime flag so normal tests and benchmark output remain stable unless explicitly enabled.
- Keep instrumentation local to `ParRelaxedEngine` and `bench_smoke`.
- Use simple counters and only high-level `std::chrono::steady_clock` timers in the first patch.
- Preserve deterministic algorithm behavior; measurement must not change repair decisions.
- Avoid expensive logging inside inner loops by accumulating counters and printing aggregate totals at the end.
- Do not immediately time every inner phase; keep the first patch small enough to avoid cluttering repair logic.

Suggested shape:

- Add a small private stats struct inside `ParRelaxedEngine` for cumulative repair diagnostics.
- Increment coarse counters at natural boundaries in `apply_update()`, `apply_batch()`, `expand_with_neighbors()`, `collect_conflicted_vertices_from_candidates()`, and `attempt_parallel_repair()`.
- Time only:
  - active-set build/expand/conflict discovery as `active_build_seconds`
  - the full bounded repair call as `repair_seconds`
- Do not split `attempt_parallel_repair()` into proposal, safety, commit, and unresolved recomputation timers in the first patch.
- Print diagnostics from `bench_smoke` only for `par_relaxed`, and preferably only when enabled with an option such as `--diagnostics 1`. If fixed output keys are preferred, keep them limited to `par_relaxed`.

Minimum useful first instrumentation:

- `repair_calls`
- `repair_rounds`
- `active_vertices_initial_total`
- `active_vertices_expanded_total`
- `conflicted_vertices_initial_total`
- `neighbor_scans`
- `commits_total`
- `repair_seconds`
- `active_build_seconds`

## 6. Benchmark scenarios to run

Use deterministic seeds for every run. Start with `--seed 1`, then repeat with a small fixed set such as `1 2 3 12345`.

Small graph smoke:

```bash
./build/benchmarks/bench_smoke --engine graph_store_only --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1
./build/benchmarks/bench_smoke --engine seq_baseline --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1
./build/benchmarks/bench_smoke --engine par_relaxed --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1 --c 2 --max-rounds 4
```

Small graph batch matrix:

```bash
for c in 2 4 8; do
  for batch in 1 4 16; do
    for rounds in 1 4; do
      ./build/benchmarks/bench_smoke --engine par_relaxed --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size "${batch}" --c "${c}" --max-rounds "${rounds}"
    done
  done
done
```

Medium graph matrix:

```bash
for engine in graph_store_only seq_baseline; do
  for batch in 1 4 16; do
    ./build/benchmarks/bench_smoke --engine "${engine}" --seed 1 --vertices 1000 --updates 1000 --delta-cap 16 --batch-size "${batch}"
  done
done

for c in 2 4 8; do
  for batch in 1 4 16; do
    for rounds in 1 4; do
      ./build/benchmarks/bench_smoke --engine par_relaxed --seed 1 --vertices 1000 --updates 1000 --delta-cap 16 --batch-size "${batch}" --c "${c}" --max-rounds "${rounds}"
    done
  done
done
```

Recommended logging:

- Save raw benchmark output under `logs/`.
- Include date, git commit, compiler, build type, host/node type, and Parlay worker/thread settings if configured.
- Repeat each key scenario at least 3 times before making conclusions from timing.

## 7. Profiling commands to run on Linux

Configure and build on the Linux cluster:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target bench_smoke
```

Plain benchmark logs:

```bash
mkdir -p logs/perf_investigation

./build/benchmarks/bench_smoke --engine par_relaxed --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 4 --c 4 --max-rounds 4 \
  2>&1 | tee logs/perf_investigation/par_relaxed_small_batch4_c4_r4.log

./build/benchmarks/bench_smoke --engine seq_baseline --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 4 \
  2>&1 | tee logs/perf_investigation/seq_baseline_small_batch4.log

./build/benchmarks/bench_smoke --engine par_relaxed --seed 1 --vertices 1000 --updates 1000 --delta-cap 16 --batch-size 4 --c 4 --max-rounds 4 \
  2>&1 | tee logs/perf_investigation/par_relaxed_medium_batch4_c4_r4.log
```

Wall-clock and memory profile with `/usr/bin/time -v`:

```bash
/usr/bin/time -v ./build/benchmarks/bench_smoke --engine par_relaxed --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 4 --c 4 --max-rounds 4 \
  2>&1 | tee logs/perf_investigation/time_par_relaxed_small_batch4_c4_r4.log

/usr/bin/time -v ./build/benchmarks/bench_smoke --engine par_relaxed --seed 1 --vertices 1000 --updates 1000 --delta-cap 16 --batch-size 4 --c 4 --max-rounds 4 \
  2>&1 | tee logs/perf_investigation/time_par_relaxed_medium_batch4_c4_r4.log
```

Optional `perf stat` on compute nodes if available:

```bash
perf stat ./build/benchmarks/bench_smoke --engine par_relaxed --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 4 --c 4 --max-rounds 4 \
  2>&1 | tee logs/perf_investigation/perf_stat_par_relaxed_small_batch4_c4_r4.log

perf stat ./build/benchmarks/bench_smoke --engine par_relaxed --seed 1 --vertices 1000 --updates 1000 --delta-cap 16 --batch-size 4 --c 4 --max-rounds 4 \
  2>&1 | tee logs/perf_investigation/perf_stat_par_relaxed_medium_batch4_c4_r4.log
```

Do not block the investigation if `perf` is unavailable on login nodes. Use plain benchmark logs and `/usr/bin/time -v` as the required baseline.

## 8. How to interpret results

If small graphs are slow but medium graphs improve:

- Treat the issue as fixed overhead from Parlay scheduling, allocation, or validation.
- Keep optimization conservative and focus on avoiding parallel repair for tiny active sets.

If both small and medium graphs are slow:

- Prioritize phase-level instrumentation inside repair.
- Look for repeated neighbor copies, excessive active expansion, and unresolved recomputation costs.

If `repair_calls` is high but `conflicted_vertices_initial` is often zero:

- Batch mode is entering repair machinery unnecessarily.
- Candidate optimization: skip repair after accepted batches when inserted endpoints create no conflicts.

If `active_vertices_after_expansion` is much larger than `conflicted_vertices_initial`:

- Active expansion is likely too broad.
- Candidate optimization: defer neighbor expansion or use a narrower conflict-driven set.

If `neighbor_scans` dominates:

- Repeated `graph_.neighbors(v)` copies and scans are likely central.
- Candidate optimization: reduce repeated neighbor materialization or cache neighbor lists within a repair call.

If proposal/safety phases dominate with tiny active sets:

- Parlay `parallel_for` overhead likely dominates.
- Candidate optimization: use sequential loops below an active-size threshold.

If validation time is a large share of total time:

- Benchmark interpretation should separate algorithm update time from final validation.
- Per-operation engine validation may also need conditional debug-only policy later, but only after correctness risk is reviewed.

If rejected batches dominate:

- Compare `updates_requested`, `updates_applied`, and `updates_rejected`.
- Interpret throughput as workload/rejection behavior as much as coloring behavior.
- Consider a later benchmark workload that controls accepted-update ratio.

## 9. Candidate optimizations, but do not implement them yet

Likely candidates after measurement:

- Add a sequential fast path inside repair when `active.size()` is below a threshold.
- In `apply_batch()`, detect whether accepted inserted edges actually created color conflicts before calling repair machinery.
- Avoid expanding active sets with all neighbors before checking actual conflicts.
- Reuse temporary buffers across rounds or across repair calls.
- Reduce repeated `graph_.neighbors(v)` materialization inside the same repair call.
- Combine or narrow conflict collection and unresolved recomputation passes.
- Track active membership without allocating an `active_index` vector of size `num_vertices` for tiny active sets.
- Tune `max_rounds` defaults if extra rounds do little useful work.
- Consider benchmark workload additions that better represent larger parallel graphs and accepted-update ratios.

Do not choose among these until measurements identify the dominant cost.

## 10. Risks of premature optimization

- A fast path may accidentally bypass required repair after accepted batches.
- Narrowing active sets may miss conflicts created by simultaneous commits.
- Reducing validation too early may hide correctness regressions in a young algorithm.
- Reusing buffers can introduce stale state bugs.
- Avoiding neighbor copies may require `GraphStore` API changes that affect broader project contracts.
- Optimizing for tiny smoke graphs may hurt the eventual larger parallel workload.
- Changing benchmark generation before measuring may obscure the current symptom.

## 11. Acceptance criteria for the investigation

The investigation is complete when:

- Linux benchmark logs cover small and medium scenarios with deterministic seeds.
- Results compare `graph_store_only`, `seq_baseline`, and `par_relaxed`.
- `par_relaxed` results include `c in {2,4,8}`, `batch_size in {1,4,16}`, and `max_rounds in {1,4}`.
- `/usr/bin/time -v` has been run for at least the key small and medium `par_relaxed batch_size=4` cases.
- Optional `perf stat` results are included if available on compute nodes.
- Added instrumentation, if implemented later, starts with coarse counters and high-level timers: `repair_calls`, `repair_rounds`, active/conflicted vertex totals, `neighbor_scans`, `commits_total`, `repair_seconds`, and `active_build_seconds`.
- Detailed inner-phase timing is deferred unless coarse metrics do not explain the issue.
- The report states whether the dominant issue is fixed overhead, allocation/copy overhead, excessive scanning, excessive repair calls, fallback behavior, validation cost, rejected workload shape, or non-representative benchmark scale.
- The report recommends one specific next coding step and names the evidence supporting it.

## Recommendation

The next step should be instrumentation, not immediate optimization.

The most likely first measurement step is to add coarse repair counters and high-level timers around active-set construction and total repair, then rerun the small `batch_size=4` and medium `batch_size=4` scenarios. Do not time every inner repair phase in the first patch; keep diagnostics focused enough that the code remains easy to read.

The recommended next coding step after this plan is accepted is a small opt-in instrumentation patch, limited to `ParRelaxedEngine` and `par_relaxed` benchmark metric printing, ideally gated by `--diagnostics 1`. Optimization should wait until those measurements identify the dominant cost.
