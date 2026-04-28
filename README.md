# Dynamic Graph Coloring (C++17 + ParlayLib)

This repository is currently at Foundation Step 1A: project skeleton and build configuration only.

## Requirements

- CMake 3.16+
- A C++17 compiler
- ParlayLib headers available via one of:
  - `-DPARLAYLIB_DIR=/path/to/parlaylib` (or its `include` directory)
  - `external/parlaylib` in this repository

No external dependencies are downloaded automatically.

## Configure

```bash
cmake -S . -B build
```

With explicit ParlayLib location:

```bash
cmake -S . -B build -DPARLAYLIB_DIR=/absolute/path/to/parlaylib
```

Optional feature toggles:

```bash
cmake -S . -B build -DDGCOLOR_BUILD_TESTS=ON -DDGCOLOR_BUILD_BENCHMARKS=ON
```

## Build

Build all enabled targets:

```bash
cmake --build build
```

Build only test placeholder:

```bash
cmake --build build --target test_foundation_placeholder
```

Build only benchmark smoke placeholder:

```bash
cmake --build build --target bench_smoke
```

## Test

```bash
ctest --test-dir build --output-on-failure
```
