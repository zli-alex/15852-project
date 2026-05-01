# Real Dataset Benchmarks

This stage prepares static edge-list datasets as deterministic update streams for the existing dynamic coloring benchmark path. It intentionally does not change the C++ engines, `GraphStore`, existing synthetic workloads, or tests.

## Why SNAP First

SNAP datasets are a good first target because many of them are simple text edge lists, are easy to cite, and include small-to-medium collaboration and communication graphs that fit early validation runs. Their plain `.txt` and `.txt.gz` formats also keep preprocessing independent from benchmark engine code.

Recommended starter datasets:

- `ca-GrQc`
- `ca-HepTh`
- `ca-CondMat`
- `ca-AstroPh`
- `email-Enron`

## Preprocessing Policy

Use `scripts/prepare_real_dataset.py` to convert a source edge list into:

- `initial_edges.txt`: one remapped edge per line as `u v`
- `updates.txt`: one update per line as `I u v` or `D u v`
- `metadata.json`: counters and parameters needed to reproduce the stream

The parser accepts plain text and `.gz` files. It skips blank lines and comment lines beginning with `#` or `%`, accepts whitespace, comma, or tab delimiters, and ignores columns after the first two tokens. Node ids are remapped to contiguous ids `0..n-1` after edge filtering and degree-cap selection.

By default, the script treats the dataset as undirected. Self-loops are dropped, duplicate undirected edges are dropped, and candidate edges are shuffled with a deterministic seed before degree-cap selection. Use `--directed` only for experiments that intentionally preserve edge orientation.

`--largest-component` is currently accepted by the CLI but does not filter the graph yet.

## Degree-Cap Policy

The preparation script enforces `--delta-cap` while constructing the initial graph and insertion stream. It greedily scans the deterministic candidate order and skips any edge whose insertion would make either endpoint exceed the cap. Deletions in mixed streams are generated only from edges already inserted by the update stream.

This policy gives `bench_smoke`-style engines a stream that is already compatible with the configured maximum degree, while preserving metadata about skipped self-loops, duplicates, and degree-cap rejections.

## Update-Stream Construction

For `--mode insertion_only`, the first accepted edges become the initial graph according to `--initial-edges`; the remaining accepted edges become insertion updates up to `--updates`.

For `--mode mixed`, the script writes insertions first, then deterministic deletions from those inserted update edges. The mixed mode currently uses about half the requested update budget for insertions and the remainder for deletions, capped by available inserted edges.

Prepared files are written under:

```text
data/prepared/<dataset_name>/seed_<seed>/
```

The output root can be changed with `--output-dir` or `PREPARED_OUTPUT_DIR` in the runner.

## Running

Prepare one dataset stream:

```bash
python3 scripts/prepare_real_dataset.py \
  --input data/raw/ca-GrQc.txt.gz \
  --name ca-GrQc \
  --delta-cap 64 \
  --initial-edges 0 \
  --updates 10000 \
  --batch-size 64 \
  --seed 1
```

Run the real-dataset stage:

```bash
DATASET_NAME=ca-GrQc \
DATASET_PATH=data/raw/ca-GrQc.txt.gz \
bash scripts/run_real_dataset_benchmarks.sh
```

Useful overrides:

```bash
DELTA_CAP=64 INITIAL_EDGES=1000 UPDATES=20000 BATCH_SIZE=64 SEEDS="1 2 3" \
ENGINES="seq_baseline seq_exact par_relaxed par_exact" MODE=insertion_only \
DATASET_NAME=ca-GrQc DATASET_PATH=data/raw/ca-GrQc.txt.gz \
bash scripts/run_real_dataset_benchmarks.sh
```

The runner configures and builds Release, runs full CTest once, prepares each seed, and then runs every requested engine only if `bench_smoke` advertises file-stream support.

## Limitations

- No dataset download is automated.
- Largest-component filtering is not implemented yet.
- The prepared format is intentionally simple, but `bench_smoke` currently needs a file-stream workload reader before the engines can consume it directly.
- The mixed stream is insertion phase followed by deletion phase, not an interleaved temporal model.
- The degree cap is applied after deterministic shuffling, so changing the seed changes which edges survive the cap.

## Validation Commands

```bash
python3 scripts/prepare_real_dataset.py --help
bash -n scripts/run_real_dataset_benchmarks.sh
```

## Next Step

If `bench_smoke` does not yet support external streams, add a workload like:

```bash
./build/benchmarks/bench_smoke \
  --workload file_stream \
  --initial-file data/prepared/ca-GrQc/seed_1/initial_edges.txt \
  --updates-file data/prepared/ca-GrQc/seed_1/updates.txt
```

That workload should load the initial edge list, initialize the selected coloring engine, apply `I u v` / `D u v` updates in batches, and report the same metrics as the existing synthetic workloads.
